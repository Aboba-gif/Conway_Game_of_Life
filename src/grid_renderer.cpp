// =============================================================
// grid_renderer.cpp — implementation of GridRenderer.
//
// See grid_renderer.hpp for the public contract.
//
// Introduced in refactor phase B5, part 2/3.
// =============================================================

#include "grid_renderer.hpp"

#include <glad/gl.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// =============================================================
// Static helpers
// =============================================================

std::string GridRenderer::load_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("GridRenderer: cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

unsigned int GridRenderer::compile_shader(unsigned int type,
                                          const std::string& src,
                                          const char*        tag) {
    // Create shader object of requested type (GL_VERTEX_SHADER / GL_FRAGMENT_SHADER).
    const GLuint sh = glCreateShader(type);
    if (sh == 0) {
        throw std::runtime_error(
            std::string("GridRenderer: glCreateShader returned 0 for ") + tag);
    }

    const char* src_ptr = src.c_str();
    const GLint src_len = static_cast<GLint>(src.size());
    glShaderSource(sh, 1, &src_ptr, &src_len);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
        glGetShaderInfoLog(sh, log_len, nullptr, log.data());
        glDeleteShader(sh);
        throw std::runtime_error(
            std::string("GridRenderer: ") + tag + " shader compile failed:\n" + log);
    }
    return sh;
}

unsigned int GridRenderer::link_program(unsigned int vs, unsigned int fs) {
    const GLuint prog = glCreateProgram();
    if (prog == 0) {
        throw std::runtime_error("GridRenderer: glCreateProgram returned 0");
    }

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    // Detach immediately — shaders live independently and can be deleted.
    glDetachShader(prog, vs);
    glDetachShader(prog, fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok == GL_FALSE) {
        GLint log_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
        glGetProgramInfoLog(prog, log_len, nullptr, log.data());
        glDeleteProgram(prog);
        throw std::runtime_error(
            "GridRenderer: program link failed:\n" + log);
    }
    return prog;
}

// =============================================================
// Ctor
// =============================================================

GridRenderer::GridRenderer(const std::string& vert_path,
                           const std::string& frag_path) {
    // --- 1. Load and compile both shaders. -------------------
    const std::string vs_src = load_text_file(vert_path);
    const std::string fs_src = load_text_file(frag_path);

    const GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src, "vertex");
    GLuint       fs = 0;
    try {
        fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }

    // --- 2. Link program. ------------------------------------
    try {
        prog_ = link_program(vs, fs);
    } catch (...) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        throw;
    }

    // Shader objects can be released now; program retains the code.
    glDeleteShader(vs);
    glDeleteShader(fs);

    // --- 3. Query uniform locations. -------------------------
    loc_grid_wh_ = glGetUniformLocation(prog_, "u_grid_wh");
    loc_vp_wh_   = glGetUniformLocation(prog_, "u_vp_wh");
    loc_center_  = glGetUniformLocation(prog_, "u_center");
    loc_zoom_    = glGetUniformLocation(prog_, "u_zoom");

    // Warn on missing uniforms — not fatal (glUniform*(-1,...) is a
    // legal no-op per GL 4.6 spec §7.6.1), but usually indicates a
    // mistake in the shader source.
    auto warn_if_missing = [](int loc, const char* name) {
        if (loc < 0) {
            std::fprintf(stderr,
                         "[GridRenderer] warning: uniform '%s' has location -1"
                         " (optimized out or misspelled)\n",
                         name);
        }
    };
    warn_if_missing(loc_grid_wh_, "u_grid_wh");
    warn_if_missing(loc_vp_wh_,   "u_vp_wh");
    warn_if_missing(loc_center_,  "u_center");
    warn_if_missing(loc_zoom_,    "u_zoom");

    // --- 4. Create an empty VAO. -----------------------------
    // Required by Core profile for glDrawArrays to be legal even
    // when we use zero vertex attributes.
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        glDeleteProgram(prog_);
        prog_ = 0;
        throw std::runtime_error("GridRenderer: glGenVertexArrays returned 0");
    }
}

// =============================================================
// Dtor
// =============================================================

GridRenderer::~GridRenderer() {
    // Safe-delete: GL silently ignores 0 handles.
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (prog_ != 0) {
        glDeleteProgram(prog_);
        prog_ = 0;
    }
}

// =============================================================
// Draw
// =============================================================

void GridRenderer::draw(unsigned int cells_ssbo,
                        int          grid_w,
                        int          grid_h,
                        int          vp_w,
                        int          vp_h,
                        float        cx,
                        float        cy,
                        float        zoom) {
    // -- 1. Validate preconditions. ---------------------------
    if (grid_w < 1 || grid_h < 1) {
        throw std::invalid_argument("GridRenderer::draw: grid dims must be >= 1");
    }
    if (vp_w < 1 || vp_h < 1) {
        throw std::invalid_argument("GridRenderer::draw: viewport dims must be >= 1");
    }
    if (!(zoom > 0.0f)) {
        throw std::invalid_argument("GridRenderer::draw: zoom must be > 0");
    }
    if (cells_ssbo == 0) {
        throw std::invalid_argument("GridRenderer::draw: cells_ssbo is 0");
    }

    // -- 2. Snapshot GL state we will modify. -----------------
    // We snapshot depth test + culling so our draw doesn't permanently
    // alter external state. This is cheap; a few dozen integers.
    GLboolean prev_depth_test    = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_cull_face     = glIsEnabled(GL_CULL_FACE);
    GLboolean prev_scissor_test  = glIsEnabled(GL_SCISSOR_TEST);

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    GLint prev_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);

    GLint prev_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);

    GLint prev_ssbo_bp0 = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,
                    kBindingCells, &prev_ssbo_bp0);

    // -- 3. Configure state for our draw. ---------------------
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, vp_w, vp_h);

    glUseProgram(prog_);
    glBindVertexArray(vao_);

    // Bind cells SSBO as read-only at binding = kBindingCells (= 0).
    // The shader declares `layout(binding = 0, std430) readonly buffer Cells`,
    // so a program-side glShaderStorageBlockBinding is unnecessary.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBindingCells, cells_ssbo);

    // -- 4. Push uniforms. ------------------------------------
    // glUniform*(-1, ...) is a legal no-op; guards are purely
    // defensive against debug builds with dead-code elimination.
    if (loc_grid_wh_ >= 0) glUniform2i(loc_grid_wh_, grid_w, grid_h);
    if (loc_vp_wh_   >= 0) glUniform2i(loc_vp_wh_,   vp_w,   vp_h);
    if (loc_center_  >= 0) glUniform2f(loc_center_,  cx,     cy);
    if (loc_zoom_    >= 0) glUniform1f(loc_zoom_,    zoom);

    // -- 5. Issue the draw. -----------------------------------
    // 3 vertices, no VBO: vertex shader synthesizes positions
    // from gl_VertexID (full-screen covering triangle).
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // -- 6. Restore previous GL state. ------------------------
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBindingCells,
                     static_cast<GLuint>(prev_ssbo_bp0));
    glBindVertexArray(static_cast<GLuint>(prev_vao));
    glUseProgram(static_cast<GLuint>(prev_program));
    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);

    if (prev_scissor_test == GL_TRUE) glEnable(GL_SCISSOR_TEST);
    if (prev_cull_face    == GL_TRUE) glEnable(GL_CULL_FACE);
    if (prev_depth_test   == GL_TRUE) glEnable(GL_DEPTH_TEST);
}
