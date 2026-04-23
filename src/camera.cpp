// =============================================================
// camera.cpp — pure 2D camera reducer implementation.
//
// See camera.hpp for mathematical model and invariants.
// =============================================================

#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace {

// Finite-number predicate. Returns true iff x is neither NaN nor Inf.
inline bool is_finite(double x) noexcept {
    // std::isfinite is sufficient on IEEE-754 platforms.
    return std::isfinite(x);
}

// Clamp zoom into [Z_MIN, Z_MAX]; also maps NaN and non-positive
// to Z_MIN (defensive).
inline double clamp_zoom(double z) noexcept {
    if (!is_finite(z) || z <= 0.0) return Camera::Z_MIN;
    return std::clamp(z, Camera::Z_MIN, Camera::Z_MAX);
}

// ----------------------------------------------------------
// Per-action handlers. Each is a pure function.
// ----------------------------------------------------------

Camera apply_pan(Camera c, const CamAction_Pan& a) noexcept {
    // Guard: NaN deltas become no-ops (invariant I_3).
    if (!is_finite(a.dx_px) || !is_finite(a.dy_px)) return c;

    // Pan in screen pixels -> world cells: dw = dp_px / z.
    // Sign: dragging the cursor to the RIGHT should make the
    // world appear to move right, i.e., the camera's center
    // shifts LEFT. Hence the minus.
    c.cx -= a.dx_px / c.z;
    c.cy -= a.dy_px / c.z;
    return c;
}

Camera apply_zoom(Camera c, const CamAction_Zoom& a) noexcept {
    // Guard NaN / non-positive factor.
    double factor = a.factor;
    if (!is_finite(factor) || factor <= 0.0) factor = 1.0;

    double sx = a.cursor_sx;
    double sy = a.cursor_sy;
    if (!is_finite(sx) || !is_finite(sy)) {
        // Degraded fallback: zoom around viewport center, which
        // degenerates to pure z-change with c unchanged.
        sx = 0.5 * static_cast<double>(c.W);
        sy = 0.5 * static_cast<double>(c.H);
    }

    // 1) World point under cursor BEFORE the zoom (Lemma 1).
    double wx, wy;
    c.screen_to_world(sx, sy, wx, wy);

    // 2) New zoom, clamped.
    const double z_new = clamp_zoom(c.z * factor);

    // 3) New center (Theorem 1).
    //    c' = w - (1/z') * (s - (W/2, H/2))
    const double half_w = 0.5 * static_cast<double>(c.W);
    const double half_h = 0.5 * static_cast<double>(c.H);
    c.cx = wx - (sx - half_w) / z_new;
    c.cy = wy - (sy - half_h) / z_new;
    c.z  = z_new;
    return c;
}

Camera apply_set_viewport(Camera c, const CamAction_SetViewport& a) noexcept {
    c.W = (a.W >= 1) ? a.W : 1;
    c.H = (a.H >= 1) ? a.H : 1;
    return c;
}

Camera apply_reset(Camera /*c*/, const CamAction_Reset& a) noexcept {
    // Produce a fresh camera centered on world origin-ish such
    // that cell (0,0) appears at screen (0,0) with z=8.
    Camera fresh;
    fresh.z  = 8.0;
    fresh.W  = (a.W >= 1) ? a.W : 1;
    fresh.H  = (a.H >= 1) ? a.H : 1;
    // Place world (W/(2z), H/(2z)) at screen center => cell (0,0)
    // ends up at screen (0,0).
    fresh.cx = 0.5 * static_cast<double>(fresh.W) / fresh.z;
    fresh.cy = 0.5 * static_cast<double>(fresh.H) / fresh.z;
    return fresh;
}

}  // namespace

// =============================================================
// Public reducer: dispatches by ADT tag.
// =============================================================
Camera camera_apply(const Camera& c, const CamAction& a) noexcept {
    if (auto p = std::get_if<CamAction_Pan>(&a))          return apply_pan(c, *p);
    if (auto p = std::get_if<CamAction_Zoom>(&a))         return apply_zoom(c, *p);
    if (auto p = std::get_if<CamAction_SetViewport>(&a))  return apply_set_viewport(c, *p);
    if (auto p = std::get_if<CamAction_Reset>(&a))        return apply_reset(c, *p);
    // Unreachable if variant is exhaustive; defensive no-op.
    return c;
}
