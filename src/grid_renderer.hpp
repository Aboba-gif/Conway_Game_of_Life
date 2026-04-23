#pragma once

// =============================================================
// grid_renderer.hpp — Stateless OpenGL renderer for Conway grid.
//
// Introduced in refactor phase B5.
// Reconstructed in B6.3 after file corruption diagnosis.
//
// Contract:
//   Renders a W x H uint32 SSBO onto the currently-bound
//   framebuffer, respecting a camera (center cx, cy, zoom).
//   The renderer OWNS the GL program and a VAO; it does NOT
//   own the cells SSBO.
//
// Header-dependency policy: <cstdint>, <string> only.
// =============================================================

#include <cstdint>
#include <string>

class GridRenderer {
public:
    GridRenderer(const std::string& vert_path,
                 const std::string& frag_path);
    ~GridRenderer();

    GridRenderer(const GridRenderer&)            = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;
    GridRenderer(GridRenderer&&)                 = delete;
    GridRenderer& operator=(GridRenderer&&)      = delete;

    // Draws a full-screen triangle; fragment shader samples the
    // SSBO using the camera transform.
    //
    // Preconditions:
    //   grid_w >= 1, grid_h >= 1, vp_w >= 1, vp_h >= 1, zoom > 0,
    //   cells_ssbo != 0.
    // Throws std::invalid_argument otherwise.
    void draw(unsigned int cells_ssbo,
              int          grid_w,
              int          grid_h,
              int          vp_w,
              int          vp_h,
              float        cx,
              float        cy,
              float        zoom);

private:
    unsigned int prog_ = 0;
    unsigned int vao_  = 0;

    int loc_grid_wh_ = -1;
    int loc_vp_wh_   = -1;
    int loc_center_  = -1;
    int loc_zoom_    = -1;

    static constexpr unsigned int kBindingCells = 0;

    static std::string  load_text_file(const std::string& path);
    static unsigned int compile_shader(unsigned int type,
                                       const std::string& src,
                                       const char*        tag);
    static unsigned int link_program(unsigned int vs, unsigned int fs);
};
