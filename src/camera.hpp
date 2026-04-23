#pragma once

// =============================================================
// camera.hpp — pure 2D camera state for grid visualization.
//
// Introduced in refactor phase B6, brick 2/4.
//
// Mathematical model (see Theorem 1, docstring below):
//
//   world -> screen:  p_s = z * (p_w - c) + (W/2, H/2)
//   screen -> world:  p_w = (p_s - (W/2, H/2)) / z + c
//
// The camera is a PURE value type: no GL, no side effects. All
// mutations go through apply() which returns a NEW Camera.
// Makes it trivially unit-testable without a window.
//
// Invariants (enforced by construction and every apply()):
//   I_1:  z  in [Z_MIN, Z_MAX]     (hard clamp)
//   I_2:  W  >= 1, H >= 1          (checked in apply(SetViewport))
//   I_3:  all components finite    (no NaN, no Inf)
// =============================================================

#include <cstdint>
#include <cmath>
#include <variant>

struct Camera {
    // ---- State --------------------------------------------
    double cx = 0.0;     // world-space center x (cells)
    double cy = 0.0;     // world-space center y (cells)
    double z  = 8.0;     // zoom (pixels per cell); default = 8 px/cell
    int    W  = 1;       // viewport width  (pixels)
    int    H  = 1;       // viewport height (pixels)

    // ---- Zoom bounds (static constants) -------------------
    static constexpr double Z_MIN = 0.0625;  // 2^-4
    static constexpr double Z_MAX = 64.0;    // 2^6

    // ---- Transforms ---------------------------------------

    // World -> screen. Pure function of state + input.
    // Returns (sx, sy) in pixel coordinates.
    // NOTE: Y is NOT flipped here; caller decides whether to
    // flip for OpenGL's bottom-left origin convention.
    inline void world_to_screen(double wx, double wy,
                                double& sx, double& sy) const noexcept {
        sx = z * (wx - cx) + 0.5 * static_cast<double>(W);
        sy = z * (wy - cy) + 0.5 * static_cast<double>(H);
    }

    // Screen -> world. Inverse of world_to_screen.
    inline void screen_to_world(double sx, double sy,
                                double& wx, double& wy) const noexcept {
        wx = (sx - 0.5 * static_cast<double>(W)) / z + cx;
        wy = (sy - 0.5 * static_cast<double>(H)) / z + cy;
    }
};

// ---- Action ADT (closed set of reducer inputs). ----------

struct CamAction_Pan        { double dx_px, dy_px; };
struct CamAction_Zoom       { double factor; double cursor_sx, cursor_sy; };
struct CamAction_SetViewport{ int W, H; };
struct CamAction_Reset      { int W, H; };

using CamAction = std::variant<
    CamAction_Pan,
    CamAction_Zoom,
    CamAction_SetViewport,
    CamAction_Reset
>;

// ---- Reducer: pure function (Camera, Action) -> Camera. --
//
// Postconditions:
//   * Returned Camera satisfies invariants I_1..I_3.
//   * Original Camera is NOT modified (value semantics).
//
// Edge cases handled:
//   * Zoom with factor <= 0 or NaN  -> returned z is clamped to Z_MIN.
//   * Pan with NaN deltas           -> returned c is unchanged.
//   * SetViewport with W<1 or H<1   -> returned (W,H) = max(input, 1).
//
Camera camera_apply(const Camera& c, const CamAction& a) noexcept;
