#version 460 core

// =============================================================
// grid.frag — Cell-grid rasterizer.
//
// For each screen pixel:
//   1. Convert gl_FragCoord to a world-space point in cell units,
//      using the camera (center, zoom) and viewport size.
//   2. Floor to obtain integer cell coordinates (cx, cy).
//   3. If inside [0,W) x [0,H): sample Cells[cy*W + cx].
//      Outside: paint a neutral "void" color so the grid bounds
//      are visible.
//   4. Additionally, draw a subtle 1-pixel gridline between cells
//      when the zoom is high enough that individual cells are
//      visible (>= 6 px/cell). Below that, skip gridlines to
//      avoid a solid-grey mess.
//
// Color scheme:
//   alive cell : white     (1.0, 1.0, 1.0)
//   dead cell  : dark grey (0.08, 0.08, 0.10)
//   gridline   : mid grey  (0.20, 0.20, 0.22)  [only when zoom big]
//   outside    : black     (0.0, 0.0, 0.0)
//
// SSBO layout (binding=0, std430, readonly):
//   uint cells[W*H], row-major, (x,y) -> cells[y*W + x].
// =============================================================

layout(binding = 0, std430) readonly buffer Cells {
    uint cells[];
};

// Uniforms (locations set via glGetUniformLocation in host code).
uniform ivec2 u_grid_wh;    // (W, H) in cells
uniform ivec2 u_vp_wh;      // viewport size in pixels
uniform vec2  u_center;     // camera center, in cell units
uniform float u_zoom;       // pixels per cell, > 0

in  vec2 v_uv;              // unused in current impl, kept for future
out vec4 frag_color;

void main() {
    // -- 1. Pixel -> world (cell-space) mapping. --------------
    // gl_FragCoord.xy is in pixel coords, origin bottom-left.
    // Flip Y so (0,0) corresponds to the top-left cell — this
    // matches the row-major layout used everywhere else.
    vec2 pix = vec2(gl_FragCoord.x,
                    float(u_vp_wh.y) - gl_FragCoord.y);

    // Vector from viewport center to this pixel, in pixels.
    vec2 from_center_px = pix - 0.5 * vec2(u_vp_wh);

    // Convert to cell units.
    vec2 world = u_center + from_center_px / u_zoom;

    // -- 2. Floor to integer cell coords. ---------------------
    ivec2 cell = ivec2(floor(world));

    // -- 3. Out-of-bounds: black "void". ----------------------
    if (cell.x < 0 || cell.x >= u_grid_wh.x ||
        cell.y < 0 || cell.y >= u_grid_wh.y) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // -- 4. Sample SSBO. --------------------------------------
    uint idx   = uint(cell.y) * uint(u_grid_wh.x) + uint(cell.x);
    uint alive = cells[idx];

    // Base color from liveness.
    vec3 col = (alive != 0u)
        ? vec3(1.00, 1.00, 1.00)
        : vec3(0.08, 0.08, 0.10);

    // -- 5. Gridlines (only when zoom is tall enough). --------
    if (u_zoom >= 6.0) {
        vec2 frac = world - vec2(cell);           // in [0,1)^2
        // 1-px line near cell borders: in cell units this is
        // 1/zoom, so a cell edge band of that width.
        float edge_px = 1.0;
        float band    = edge_px / u_zoom;
        if (frac.x < band || frac.x > (1.0 - band) ||
            frac.y < band || frac.y > (1.0 - band)) {
            col = vec3(0.20, 0.20, 0.22);
        }
    }

    frag_color = vec4(col, 1.0);
}
