#pragma once

// =============================================================
// life_app.hpp — main-loop driver for the Conway's Life viewer.
// Introduced in refactor phase B6, brick 3/4.
//
// Architecture: Elm-style reducer over AppState.
//   (AppState, WinEvent | tick) -> AppState  (pure-ish)
//   AppState -> GL draw calls                (impure)
//
// Dependency order (inside ctor):
//   1. GLContext (headless)   — owned internally
//   2. GLWindow  (visible)    — owned, binds GL current
//   3. GLComputeEngine        — owned, needs GL current
//   4. GridRenderer           — owned, needs GL current
//
// Event routing: see handle_event() in .cpp.
// =============================================================

#include "camera.hpp"
#include "gl_context.hpp"
#include "gl_window.hpp"
#include "gl_compute_engine.hpp"
#include "grid_renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct LifeAppConfig {
    int          grid_w        = 256;
    int          grid_h        = 256;
    int          win_w         = 1024;
    int          win_h         = 768;
    double       ticks_per_sec = 10.0;
    bool         start_paused  = true;
    std::uint32_t random_seed  = 0x5EEDu; // 0 => start empty
    double       fill_prob     = 0.25;
    std::string  title         = "LifeGL";
    std::string  kernel_path   = "kernels/life.comp";
    std::string  vert_path     = "kernels/grid.vert";
    std::string  frag_path     = "kernels/grid.frag";
};

class LifeApp {
public:
    // Constructs all subsystems in-order. Throws std::runtime_error
    // on any failure (rollback is automatic via RAII).
    explicit LifeApp(const LifeAppConfig& cfg);
    ~LifeApp();

    LifeApp(const LifeApp&)            = delete;
    LifeApp& operator=(const LifeApp&) = delete;
    LifeApp(LifeApp&&)                 = delete;
    LifeApp& operator=(LifeApp&&)      = delete;

    // Blocking main loop. Returns exit code (0 = success).
    int run();

    // ---- Test hooks ------------------------------------------

    // One iteration: poll, tick, render. Returns false to stop.
    // forced_dt_sec >= 0 overrides wall-clock dt for determinism.
    bool step_one_frame(double forced_dt_sec = -1.0);

    std::uint64_t generation() const noexcept { return generation_; }
    std::uint64_t frame()      const noexcept { return frame_; }
    bool          paused()     const noexcept { return paused_; }
    const Camera& camera()     const noexcept { return cam_; }
    ILifeEngine&  engine()           noexcept { return *engine_; }

    // Synthetic event injection (tests only).
    void inject_event(const WinEvent& e);

private:
    // ---- Owned subsystems (order matters: dtor reverse) ------
    GLContext                       ctx_;
    std::unique_ptr<GLWindow>       win_;
    std::unique_ptr<GLComputeEngine> engine_;
    std::unique_ptr<GridRenderer>   renderer_;

    LifeAppConfig cfg_;
    Camera        cam_;
    bool          paused_;
    double        ticks_per_sec_;

    std::uint64_t generation_  = 0;
    std::uint64_t frame_       = 0;
    double        accum_sec_   = 0.0;
    bool          should_quit_ = false;

    struct Mouse {
        int  last_x = 0, last_y = 0;
        bool dragging_right = false;
    } mouse_;

    std::int64_t  qpc_last_ = 0;
    std::int64_t  qpc_freq_ = 1;

    void   handle_event(const WinEvent& e);
    void   tick_simulation(double dt_sec);
    void   render_frame();
    void   screen_to_cell(int sx, int sy, int& cx, int& cy) const;
    double query_dt_sec();

    static constexpr double kMaxFrameDt = 0.25;
};
