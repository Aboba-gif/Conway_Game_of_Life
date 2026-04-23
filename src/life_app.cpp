// =============================================================
// life_app.cpp — implementation of LifeApp.
// See life_app.hpp for architectural contract.
// =============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <glad/gl.h>

#include "life_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <variant>

// =============================================================
// Ctor
// =============================================================
LifeApp::LifeApp(const LifeAppConfig& cfg)
    : ctx_(/*debug=*/true)
    , cfg_(cfg)
    , paused_(cfg.start_paused)
    , ticks_per_sec_(std::max(cfg.ticks_per_sec, 1e-3))
{
    // 1) Visible window. Binds GL current to this window's HDC.
    win_ = std::make_unique<GLWindow>(ctx_, cfg.win_w, cfg.win_h, cfg.title);

    // 2) Engine (GL must be current — done by GLWindow ctor).
    engine_ = std::make_unique<GLComputeEngine>(
        cfg.grid_w, cfg.grid_h, cfg.kernel_path);

    // 3) Renderer (needs GL current).
    renderer_ = std::make_unique<GridRenderer>(cfg.vert_path, cfg.frag_path);

    // 4) Camera: fit-to-window.
    cam_.W = cfg.win_w;
    cam_.H = cfg.win_h;
    const double zx = static_cast<double>(cfg.win_w) / cfg.grid_w;
    const double zy = static_cast<double>(cfg.win_h) / cfg.grid_h;
    cam_.z  = std::clamp(std::min(zx, zy), Camera::Z_MIN, Camera::Z_MAX);
    cam_.cx = 0.5 * cfg.grid_w;
    cam_.cy = 0.5 * cfg.grid_h;

    // 5) Seed state.
    if (cfg.random_seed != 0) {
        engine_->randomize(cfg.random_seed, cfg.fill_prob);
    }
    // else: engine ctor already cleared SSBOs to zero.

    // 6) Wall clock.
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    qpc_freq_ = f.QuadPart;
    qpc_last_ = t.QuadPart;

    std::printf("[LifeApp] Grid %dx%d, Window %dx%d, %.1f ticks/s, seed=0x%lx\n",
                cfg.grid_w, cfg.grid_h, cfg.win_w, cfg.win_h,
                ticks_per_sec_,
                static_cast<unsigned long>(cfg.random_seed));
    std::printf("[LifeApp] Controls:\n"
                "  [Space]  pause/resume\n"
                "  [Right]  single step (when paused)\n"
                "  [R]      randomize\n"
                "  [C]      clear\n"
                "  [F]      reset camera\n"
                "  [+]/[-]  faster/slower sim\n"
                "  [Esc]    quit\n"
                "  [LMB]    toggle cell\n"
                "  [RMB]    drag to pan\n"
                "  [Wheel]  zoom around cursor\n");
}

LifeApp::~LifeApp() = default;

// =============================================================
// Timing
// =============================================================
double LifeApp::query_dt_sec() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    const std::int64_t now = t.QuadPart;
    const std::int64_t dt_ticks = now - qpc_last_;
    qpc_last_ = now;
    double dt = static_cast<double>(dt_ticks) /
                static_cast<double>(qpc_freq_);
    if (dt > kMaxFrameDt) dt = kMaxFrameDt;
    if (dt < 0.0)         dt = 0.0;
    return dt;
}

// =============================================================
// Screen -> cell
// =============================================================
void LifeApp::screen_to_cell(int sx, int sy, int& cx, int& cy) const {
    double wx = 0.0, wy = 0.0;
    cam_.screen_to_world(static_cast<double>(sx),
                         static_cast<double>(sy), wx, wy);
    cx = static_cast<int>(std::floor(wx));
    cy = static_cast<int>(std::floor(wy));
}

// =============================================================
// Event routing
// =============================================================
void LifeApp::handle_event(const WinEvent& e) {
    std::visit([this](const auto& ev) {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, WinEvent_Close>) {
            should_quit_ = true;
        }
        else if constexpr (std::is_same_v<T, WinEvent_Resize>) {
            if (ev.w >= 1 && ev.h >= 1) {
                cam_ = camera_apply(cam_, CamAction_SetViewport{ev.w, ev.h});
                // NEW: пересчитать зум fit-to-window при resize
                const double zx = static_cast<double>(ev.w) / cfg_.grid_w;
                const double zy = static_cast<double>(ev.h) / cfg_.grid_h;
                cam_.z = std::clamp(std::min(zx, zy),
                                    Camera::Z_MIN, Camera::Z_MAX);
                cam_.cx = 0.5 * cfg_.grid_w;
                cam_.cy = 0.5 * cfg_.grid_h;
                glViewport(0, 0, ev.w, ev.h);
            }
        }
        else if constexpr (std::is_same_v<T, WinEvent_KeyDown>) {
            switch (ev.vk) {
                case VK_ESCAPE: should_quit_ = true; break;
                case VK_SPACE:  paused_ = !paused_; accum_sec_ = 0.0; break;
                case VK_RIGHT:
                    if (paused_) { engine_->step(); ++generation_; }
                    break;
                case 'R': {
                    const std::uint32_t s =
                        static_cast<std::uint32_t>(generation_ + 1u) *
                        0x9E3779B9u;
                    engine_->randomize(s, cfg_.fill_prob);
                    generation_ = 0;
                    break;
                }
                case 'C': {
                    // clear via upload of all-zero pattern
                    std::vector<std::uint8_t> zero(
                        static_cast<std::size_t>(cfg_.grid_w) * cfg_.grid_h, 0u);
                    engine_->upload_host(zero);
                    generation_ = 0;
                    break;
                }
                case 'F': {
                    cam_ = camera_apply(cam_, CamAction_Reset{cam_.W, cam_.H});
                    const double zx = static_cast<double>(cam_.W) / cfg_.grid_w;
                    const double zy = static_cast<double>(cam_.H) / cfg_.grid_h;
                    cam_.z  = std::clamp(std::min(zx, zy),
                                         Camera::Z_MIN, Camera::Z_MAX);
                    cam_.cx = 0.5 * cfg_.grid_w;
                    cam_.cy = 0.5 * cfg_.grid_h;
                    break;
                }
                case VK_OEM_PLUS:
                case VK_ADD:
                    ticks_per_sec_ = std::min(ticks_per_sec_ * 2.0, 1000.0);
                    std::printf("[LifeApp] ticks/s = %.2f\n", ticks_per_sec_);
                    break;
                case VK_OEM_MINUS:
                case VK_SUBTRACT:
                    ticks_per_sec_ = std::max(ticks_per_sec_ * 0.5, 0.1);
                    std::printf("[LifeApp] ticks/s = %.2f\n", ticks_per_sec_);
                    break;
                default: break;
            }
        }
        else if constexpr (std::is_same_v<T, WinEvent_KeyUp>) {
            (void)ev;
        }
        else if constexpr (std::is_same_v<T, WinEvent_MouseDown>) {
            mouse_.last_x = ev.x;
            mouse_.last_y = ev.y;
            if (ev.button == 0) {
                int cx = 0, cy = 0;
                screen_to_cell(ev.x, ev.y, cx, cy);
                if (cx >= 0 && cx < cfg_.grid_w &&
                    cy >= 0 && cy < cfg_.grid_h) {
                    const bool was = engine_->get_cell(cx, cy);
                    engine_->paint_cell(cx, cy, !was);
                }
            } else if (ev.button == 1) {
                mouse_.dragging_right = true;
            }
        }
        else if constexpr (std::is_same_v<T, WinEvent_MouseUp>) {
            if (ev.button == 1) mouse_.dragging_right = false;
        }
        else if constexpr (std::is_same_v<T, WinEvent_MouseMove>) {
            const int dx = ev.x - mouse_.last_x;
            const int dy = ev.y - mouse_.last_y;
            mouse_.last_x = ev.x;
            mouse_.last_y = ev.y;
            if (mouse_.dragging_right) {
                cam_ = camera_apply(cam_,
                    CamAction_Pan{static_cast<double>(dx),
                                  static_cast<double>(dy)});
            }
        }
        else if constexpr (std::is_same_v<T, WinEvent_Wheel>) {
            const double factor = std::pow(1.2, ev.ticks);
            cam_ = camera_apply(cam_,
                CamAction_Zoom{factor,
                               static_cast<double>(mouse_.last_x),
                               static_cast<double>(mouse_.last_y)});
        }
    }, e);
}

void LifeApp::inject_event(const WinEvent& e) {
    handle_event(e);
}

// =============================================================
// Fixed-timestep simulation (Gaffer 2004)
// =============================================================
void LifeApp::tick_simulation(double dt_sec) {
    if (paused_) {
        accum_sec_ = 0.0;
        return;
    }
    accum_sec_ += dt_sec;
    const double dtau = 1.0 / ticks_per_sec_;

    int iter = 0;
    constexpr int kMaxIter = 16;
    while (accum_sec_ >= dtau && iter < kMaxIter) {
        engine_->step();
        ++generation_;
        accum_sec_ -= dtau;
        ++iter;
    }
    if (iter >= kMaxIter) {
        accum_sec_ = 0.0;  // drop excess debt
    }
}

// =============================================================
// Render
// =============================================================
void LifeApp::render_frame() {
    glViewport(0, 0, cam_.W, cam_.H);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const unsigned int cells_ssbo = engine_->current_gl_buffer();

    renderer_->draw(cells_ssbo,
                    cfg_.grid_w, cfg_.grid_h,
                    cam_.W,       cam_.H,
                    static_cast<float>(cam_.cx),
                    static_cast<float>(cam_.cy),
                    static_cast<float>(cam_.z));

    win_->swap_buffers();
    ++frame_;
}

// =============================================================
// Per-frame entry
// =============================================================
bool LifeApp::step_one_frame(double forced_dt_sec) {
    win_->poll_events();
    for (const auto& ev : win_->drain_events()) {
        handle_event(ev);
    }
    if (should_quit_ || win_->should_close()) return false;

    const double dt = (forced_dt_sec >= 0.0) ? forced_dt_sec
                                             : query_dt_sec();
    tick_simulation(dt);
    render_frame();
    return true;
}

// =============================================================
// Main loop
// =============================================================
int LifeApp::run() {
    while (step_one_frame()) { /* empty */ }
    std::printf("[LifeApp] exit: generation=%llu, frames=%llu\n",
                static_cast<unsigned long long>(generation_),
                static_cast<unsigned long long>(frame_));
    return 0;
}
