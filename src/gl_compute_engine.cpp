// =============================================================
// gl_compute_engine.cpp
// Implementation of GLComputeEngine.
// Reconstructed in B6.3 after corruption diagnosis (read_host
// body was empty, get_cell/paint_cell used wrong field names).
// =============================================================

#include "gl_compute_engine.hpp"

#include <glad/gl.h>

#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr unsigned int ceil_div(int a, int b) noexcept {
    return static_cast<unsigned int>((a + b - 1) / b);
}

constexpr int    kTileSize   = 16;
constexpr GLint  kLocWidth   = 0;
constexpr GLint  kLocHeight  = 1;
constexpr GLuint kBindingIn  = 0;
constexpr GLuint kBindingOut = 1;

std::string fetch_shader_info_log(GLuint shader) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if (len <= 0) return "<empty>";
    std::string log(static_cast<size_t>(len), '\0');
    glGetShaderInfoLog(shader, len, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') log.pop_back();
    return log;
}

std::string fetch_program_info_log(GLuint prog) {
    GLint len = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
    if (len <= 0) return "<empty>";
    std::string log(static_cast<size_t>(len), '\0');
    glGetProgramInfoLog(prog, len, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') log.pop_back();
    return log;
}

std::string gl_err_to_hex(GLenum e) {
    std::ostringstream os; os << "0x" << std::hex << e;
    return os.str();
}

} // namespace

// ================================================================
//  File I/O + shader compilation
// ================================================================

std::string GLComputeEngine::load_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("GLComputeEngine: cannot open shader file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) {
        throw std::runtime_error("GLComputeEngine: I/O error reading: " + path);
    }
    return ss.str();
}

unsigned int GLComputeEngine::compile_compute_shader(const std::string& src) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (shader == 0) {
        throw std::runtime_error(
            "GLComputeEngine: glCreateShader(GL_COMPUTE_SHADER) returned 0. "
            "Context likely not 4.3+ Core or not current.");
    }
    const char* src_ptr = src.c_str();
    const GLint src_len = static_cast<GLint>(src.size());
    glShaderSource(shader, 1, &src_ptr, &src_len);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        const std::string log = fetch_shader_info_log(shader);
        glDeleteShader(shader);
        throw std::runtime_error(
            "GLComputeEngine: compute shader compile FAILED.\n"
            "---- InfoLog ----\n" + log);
    }
    return shader;
}

unsigned int GLComputeEngine::link_compute_program(unsigned int shader) {
    GLuint prog = glCreateProgram();
    if (prog == 0) {
        glDeleteShader(shader);
        throw std::runtime_error("GLComputeEngine: glCreateProgram returned 0.");
    }
    glAttachShader(prog, shader);
    glLinkProgram(prog);

    GLint status = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        const std::string log = fetch_program_info_log(prog);
        glDetachShader(prog, shader);
        glDeleteShader(shader);
        glDeleteProgram(prog);
        throw std::runtime_error(
            "GLComputeEngine: compute program link FAILED.\n"
            "---- InfoLog ----\n" + log);
    }
    glDetachShader(prog, shader);
    glDeleteShader(shader);
    return prog;
}

// ================================================================
//  Ctor / Dtor
// ================================================================

GLComputeEngine::GLComputeEngine(int width, int height, const std::string& kernel_path)
    : w_(width), h_(height)
{
    if (w_ < 1 || h_ < 1) {
        throw std::invalid_argument(
            "GLComputeEngine: grid dimensions must be >= 1 (got " +
            std::to_string(w_) + "x" + std::to_string(h_) + ")");
    }
    constexpr std::int64_t kMaxCells = 1LL << 28;
    const std::int64_t cells =
        static_cast<std::int64_t>(w_) * static_cast<std::int64_t>(h_);
    if (cells > kMaxCells) {
        throw std::invalid_argument(
            "GLComputeEngine: grid too large: " +
            std::to_string(w_) + "x" + std::to_string(h_));
    }

    byte_size_ = static_cast<std::size_t>(cells) * sizeof(std::uint32_t);
    wg_x_      = ceil_div(w_, kTileSize);
    wg_y_      = ceil_div(h_, kTileSize);

    const std::string src = load_text_file(kernel_path);
    const GLuint shader   = compile_compute_shader(src);
    prog_                 = link_compute_program(shader);

    try {
        glCreateBuffers(2, ssbo_);
        if (ssbo_[0] == 0 || ssbo_[1] == 0) {
            throw std::runtime_error(
                "GLComputeEngine: glCreateBuffers returned 0 for SSBO(s).");
        }
        glNamedBufferStorage(ssbo_[0], static_cast<GLsizeiptr>(byte_size_),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);
        glNamedBufferStorage(ssbo_[1], static_cast<GLsizeiptr>(byte_size_),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);

        const std::uint32_t zero = 0u;
        glClearNamedBufferData(ssbo_[0], GL_R32UI, GL_RED_INTEGER,
                               GL_UNSIGNED_INT, &zero);
        glClearNamedBufferData(ssbo_[1], GL_R32UI, GL_RED_INTEGER,
                               GL_UNSIGNED_INT, &zero);

        const GLenum e = glGetError();
        if (e != GL_NO_ERROR) {
            throw std::runtime_error(
                "GLComputeEngine: GL error during SSBO init: " +
                gl_err_to_hex(e));
        }
    } catch (...) {
        if (ssbo_[0] || ssbo_[1]) {
            glDeleteBuffers(2, ssbo_);
            ssbo_[0] = ssbo_[1] = 0;
        }
        if (prog_) {
            glDeleteProgram(prog_);
            prog_ = 0;
        }
        throw;
    }

    current_    = 0;
    generation_ = 0;
}

GLComputeEngine::~GLComputeEngine() {
    if (ssbo_[0] || ssbo_[1]) {
        glDeleteBuffers(2, ssbo_);
        ssbo_[0] = ssbo_[1] = 0;
    }
    if (prog_) {
        glDeleteProgram(prog_);
        prog_ = 0;
    }
}

// ================================================================
//  State initialization
// ================================================================

void GLComputeEngine::randomize(std::uint32_t seed, double p) {
    if (p < 0.0 || p > 1.0) {
        throw std::invalid_argument(
            "GLComputeEngine::randomize: p must be in [0,1], got " +
            std::to_string(p));
    }
    const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    std::vector<std::uint32_t> scratch(n, 0u);

    std::mt19937 rng(seed);
    if (p >= 1.0) {
        for (std::size_t i = 0; i < n; ++i) scratch[i] = 1u;
    } else if (p > 0.0) {
        std::bernoulli_distribution dist(p);
        for (std::size_t i = 0; i < n; ++i) scratch[i] = dist(rng) ? 1u : 0u;
    }

    glNamedBufferSubData(ssbo_[current_], 0,
                         static_cast<GLsizeiptr>(byte_size_),
                         scratch.data());
    generation_ = 0;
}

void GLComputeEngine::upload_host(const std::vector<std::uint8_t>& pattern) {
    const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    if (pattern.size() != n) {
        throw std::invalid_argument(
            "GLComputeEngine::upload_host: pattern size " +
            std::to_string(pattern.size()) +
            " != w*h = " + std::to_string(n));
    }
    std::vector<std::uint32_t> scratch(n);
    for (std::size_t i = 0; i < n; ++i) {
        scratch[i] = pattern[i] ? 1u : 0u;
    }
    glNamedBufferSubData(ssbo_[current_], 0,
                         static_cast<GLsizeiptr>(byte_size_),
                         scratch.data());
    generation_ = 0;
}

// ================================================================
//  Simulation step
// ================================================================

void GLComputeEngine::step() {
    const int next = 1 - current_;
    glUseProgram(prog_);
    glUniform1i(kLocWidth,  w_);
    glUniform1i(kLocHeight, h_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBindingIn,  ssbo_[current_]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBindingOut, ssbo_[next]);
    glDispatchCompute(wg_x_, wg_y_, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
    current_ = next;
    ++generation_;
}

// ================================================================
//  Readback  [B6.3 FIX: body was empty, out was always zeros]
// ================================================================

void GLComputeEngine::read_host(std::vector<std::uint8_t>& out) {
    const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    std::vector<std::uint32_t> scratch(n);

    glGetNamedBufferSubData(ssbo_[current_], 0,
                            static_cast<GLsizeiptr>(byte_size_),
                            scratch.data());

    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = scratch[i] ? 1u : 0u;  // <-- this line was MISSING
    }
}

// ================================================================
//  Per-cell access (B6.3) [FIX: correct field names]
// ================================================================

bool GLComputeEngine::get_cell(int x, int y) {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return false;

    std::uint32_t word = 0;
    const GLintptr offset = static_cast<GLintptr>(
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) +
         static_cast<std::size_t>(x)) * sizeof(std::uint32_t));
    glGetNamedBufferSubData(ssbo_[current_], offset,
                            sizeof(std::uint32_t), &word);
    return word != 0u;
}

void GLComputeEngine::paint_cell(int x, int y, bool alive) {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
    const std::uint32_t word = alive ? 1u : 0u;
    const GLintptr offset = static_cast<GLintptr>(
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) +
         static_cast<std::size_t>(x)) * sizeof(std::uint32_t));
    glNamedBufferSubData(ssbo_[current_], offset,
                         sizeof(std::uint32_t), &word);
}
