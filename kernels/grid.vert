#version 460 core

// =============================================================
// grid.vert — Full-screen triangle synthesizer.
//
// Emits a single covering triangle with vertices in clip space:
//     id=0 : (-1, -1)
//     id=1 : ( 3, -1)
//     id=2 : (-1,  3)
// This triangle exactly covers the [-1, 1] x [-1, 1] NDC square,
// with 25% overdraw (acceptable; trivial cost).
//
// No vertex attributes are used; the VAO passed in by the host
// is empty. Positions are derived purely from gl_VertexID.
//
// A flat-interpolated vec2 v_uv is exported in normalized
// device coordinates [0,1] x [0,1] for downstream use (the
// fragment shader recomputes its own pixel coordinate via
// gl_FragCoord, so v_uv is currently informational only).
// =============================================================

out vec2 v_uv;

void main() {
    // id -> (x, y) mapping for a covering-triangle.
    //   id=0 -> (0, 0)
    //   id=1 -> (2, 0)
    //   id=2 -> (0, 2)
    vec2 xy = vec2(
        float((gl_VertexID & 1) << 1),   // 0, 2, 0
        float( gl_VertexID & 2)          // 0, 0, 2
    );

    // Map [0,2]x[0,2] -> clip-space [-1,3]x[-1,3], which covers
    // the visible square [-1,1]x[-1,1].
    gl_Position = vec4(xy * 2.0 - 1.0, 0.0, 1.0);

    // UV in [0,1]x[0,1], extending to [0,2]x[0,2] at the off-screen
    // corner — clamped naturally because the fragment shader uses
    // gl_FragCoord, not v_uv, for cell lookup.
    v_uv = xy * 0.5;
}
