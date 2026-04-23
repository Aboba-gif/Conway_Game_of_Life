#pragma once

// =============================================================
// gl_window.hpp — Visible OpenGL window on top of GLContext.
//
// Introduced in refactor phase B6, brick 1/4.
//
// Semantic delta vs. GLContext (phase B3):
//   * GLContext creates an INVISIBLE 1x1 top-level window whose
//     sole purpose is to own an HDC/HGLRC pair for headless GL.
//   * GLWindow creates a VISIBLE resizable client-area window,
//     registers a custom WndProc, and exposes a polling API for
//     input events. It does NOT own the GL context — it receives
//     a reference to an existing GLContext and makes its own HDC
//     compatible with the GL pixel-format.
//
// Lifecycle (RAII):
//   GLContext ctx(/*debug=*/true);   // headless context (owned).
//   GLWindow  win(ctx, 1280, 720, "LifeGL");
//   while (!win.should_close()) {
//       win.poll_events();
//       for (const auto& e : win.drain_events()) { ... }
//       // ... render ...
//       win.swap_buffers();
//   }
//
// Threading:
//   Tied to the thread that constructed both GLContext and the
//   window. Message pump (GetMessageW/DispatchMessageW) runs on
//   this thread. Not movable, not copyable.
//
// Header-dependency policy:
//   Depends only on <cstdint>, <string>, <vector>, <variant>.
//   Does NOT include <windows.h> or glad/gl.h.
// =============================================================

#include "gl_context.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// ----- Event payloads (POD, value-semantics). -----

struct WinEvent_Close        { };
struct WinEvent_Resize       { int w, h; };
struct WinEvent_KeyDown      { int vk; };    // virtual-key code (Win32 VK_*)
struct WinEvent_KeyUp        { int vk; };
struct WinEvent_MouseMove    { int x, y; };
struct WinEvent_MouseDown    { int button; int x, y; };  // 0=L, 1=R, 2=M
struct WinEvent_MouseUp      { int button; int x, y; };
struct WinEvent_Wheel        { int ticks; };             // +1 per 120 delta

using WinEvent = std::variant<
    WinEvent_Close,
    WinEvent_Resize,
    WinEvent_KeyDown,
    WinEvent_KeyUp,
    WinEvent_MouseMove,
    WinEvent_MouseDown,
    WinEvent_MouseUp,
    WinEvent_Wheel
>;

// ----- Forward declarations to avoid <windows.h>. -----
struct HWND__;  using HWND_t = HWND__*;
struct HDC__;   using HDC_t  = HDC__*;

class GLWindow {
public:
    // Constructs a visible top-level window that shares the same
    // pixel-format family as ctx. The ctx's GL context will be
    // re-bound (wglMakeCurrent) to this window's HDC, so after
    // the ctor returns, GL draws from this window.
    //
    // @param ctx    The already-initialized GL context to attach.
    // @param w, h   Initial client-area size in pixels (>= 1).
    // @param title  UTF-8 window title.
    //
    // @throws std::invalid_argument if w<1 or h<1.
    // @throws std::runtime_error on any WinAPI or WGL failure.
    GLWindow(GLContext& ctx, int w, int h, const std::string& title);

    // Destroys the window. GL context remains valid but un-bound.
    ~GLWindow();

    // Non-copyable, non-movable.
    GLWindow(const GLWindow&)            = delete;
    GLWindow& operator=(const GLWindow&) = delete;
    GLWindow(GLWindow&&)                 = delete;
    GLWindow& operator=(GLWindow&&)      = delete;

    // ---- Message pump ---------------------------------------

    // Drains all pending messages from the thread's message queue,
    // invokes the internal WndProc, and appends translated events
    // to the internal queue. Non-blocking: returns after queue
    // is empty.
    void poll_events();

    // Moves all accumulated events out of the internal queue.
    // Caller consumes the returned vector. After this call the
    // internal queue is empty. Thread-safe only vs. self.
    std::vector<WinEvent> drain_events();

    // Returns true if WM_CLOSE has been seen. The window itself
    // is NOT destroyed until the dtor runs — this gives the main
    // loop a chance to save state.
    bool should_close() const noexcept { return should_close_; }

    // Swaps front/back buffers on the window's HDC.
    void swap_buffers();

    // Current client-area size (updated on WM_SIZE).
    int width()  const noexcept { return width_;  }
    int height() const noexcept { return height_; }

private:
    // Win32 resources.
    HWND_t hwnd_ = nullptr;
    HDC_t  hdc_  = nullptr;

    // Reference to the context whose HGLRC we re-bind.
    GLContext* ctx_ = nullptr;

    // Accumulated input events (populated by WndProc via thunks).
    std::vector<WinEvent> queue_;

    // Lifecycle flags.
    bool should_close_ = false;

    // Current client-area size.
    int width_  = 0;
    int height_ = 0;

    // Internal: WndProc thunk (C-linkage static → dispatches to 'this').
    static long long __stdcall wnd_proc_thunk(void* hwnd, unsigned int msg,
                                              unsigned long long wp,
                                              long long lp);
    // Real per-instance handler.
    long long handle_msg(void* hwnd, unsigned int msg,
                         unsigned long long wp, long long lp);

    // Unique window-class name.
    static constexpr const wchar_t* kWndClassName = L"LifeGLVisibleWindow_v1";
};
