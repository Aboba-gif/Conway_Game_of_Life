// =============================================================
// gl_window.cpp — implementation of GLWindow (see gl_window.hpp).
//
// Introduced in refactor phase B6, brick 1/4.
//
// Implementation notes:
//   * Uses the trampoline pattern: a static WndProc stores the
//     instance pointer in GWLP_USERDATA on WM_NCCREATE and then
//     forwards all subsequent messages to handle_msg().
//   * Re-uses the pixel-format and HGLRC from the passed-in
//     GLContext (see Lemma 2, gl_window.hpp comments).
// =============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM, GET_Y_LPARAM

#include <glad/gl.h>    // glad must follow windows.h (for wgl typedefs)

#include "gl_window.hpp"

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>

// Declared in gl_context.cpp, reused for WinAPI error throw.
[[noreturn]] void gl_throw_winapi(const char* where);

namespace {

// ---- Legacy PFD mirroring the one in gl_context.cpp ----
// MUST stay in sync with gl_context.cpp::make_legacy_pfd().
// Duplicated rather than shared to keep gl_window.cpp free
// of any dependency on gl_context internal helpers.
PIXELFORMATDESCRIPTOR make_legacy_pfd() {
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;
    return pfd;
}

// UTF-8 -> UTF-16 for window titles.
std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0,
                                      s.data(),
                                      static_cast<int>(s.size()),
                                      nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

// Extract mouse button index from Win32 message.
int button_from_msg(UINT msg) {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: return 0;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 1;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 2;
        default: return -1;
    }
}

}  // namespace

// =============================================================
// Static trampoline: bridges C-linkage Win32 WndProc contract
// to a per-instance handle_msg() method.
// =============================================================
LRESULT CALLBACK
GLWindow::wnd_proc_thunk(void* hwnd_void, UINT msg, WPARAM wp, LPARAM lp) {
    HWND hwnd = reinterpret_cast<HWND>(hwnd_void);

    GLWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        // Extract 'this' from CreateWindowEx lpParam.
        // Per MSDN: lParam of WM_NCCREATE points to a CREATESTRUCT
        // whose lpCreateParams field holds the value we passed as
        // the last argument to CreateWindowExW.
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<GLWindow*>(cs->lpCreateParams);
        SetLastError(0);
        if (SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self)) == 0 &&
            GetLastError() != 0) {
            // Defensive: SetWindowLongPtrW failed. Fall through
            // to DefWindowProcW so CreateWindowExW returns NULL
            // and our ctor will throw.
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    } else {
        // For every non-NCCREATE message, the pointer may or may
        // not be set yet (WM_GETMINMAXINFO fires BEFORE NCCREATE).
        self = reinterpret_cast<GLWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return static_cast<LRESULT>(
            self->handle_msg(hwnd, msg, static_cast<uint64_t>(wp),
                             static_cast<int64_t>(lp)));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============================================================
// Instance-level message handler.
// =============================================================
long long
GLWindow::handle_msg(void* hwnd_void, unsigned int msg,
                     unsigned long long wp, long long lp) {
    HWND   hwnd    = reinterpret_cast<HWND>(hwnd_void);
    WPARAM wparam  = static_cast<WPARAM>(wp);
    LPARAM lparam  = static_cast<LPARAM>(lp);

    switch (msg) {

    case WM_CLOSE:
        // User clicked [X] OR Alt-F4. We DO NOT destroy the HWND
        // here — that is the dtor's job. Instead we set the flag
        // so the main loop's next iteration observes it.
        should_close_ = true;
        queue_.push_back(WinEvent_Close{});
        return 0;

    case WM_DESTROY:
        // Final notification. Safe to post quit — but since we
        // run our own pump (not GetMessage-until-WM_QUIT), we
        // simply ignore. Forwarding to DefWindowProcW is harmless.
        return 0;

    case WM_SIZE: {
        const int new_w = LOWORD(lparam);
        const int new_h = HIWORD(lparam);
        // Ignore zero-area resizes (minimize fires WM_SIZE w/ 0,0).
        if (new_w >= 1 && new_h >= 1 &&
            (new_w != width_ || new_h != height_)) {
            width_  = new_w;
            height_ = new_h;
            queue_.push_back(WinEvent_Resize{new_w, new_h});
        }
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        // Ignore auto-repeat (bit 30 of lParam set on repeat).
        const bool is_repeat = (lparam & (1LL << 30)) != 0;
        if (!is_repeat) {
            queue_.push_back(WinEvent_KeyDown{static_cast<int>(wparam)});
        }
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
        queue_.push_back(WinEvent_KeyUp{static_cast<int>(wparam)});
        return 0;

    case WM_MOUSEMOVE:
        queue_.push_back(WinEvent_MouseMove{
            GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;

    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        // Capture mouse so we receive ...UP even if user drags
        // cursor outside the client area.
        SetCapture(hwnd);
        queue_.push_back(WinEvent_MouseDown{
            button_from_msg(msg),
            GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;

    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        ReleaseCapture();
        queue_.push_back(WinEvent_MouseUp{
            button_from_msg(msg),
            GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        // One notch == WHEEL_DELTA (120). We translate to "ticks".
        queue_.push_back(WinEvent_Wheel{delta / WHEEL_DELTA});
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// =============================================================
// Ctor
// =============================================================
GLWindow::GLWindow(GLContext& ctx, int w, int h, const std::string& title) {
    if (w < 1 || h < 1) {
        throw std::invalid_argument(
            "GLWindow: width/height must be >= 1 (got " +
            std::to_string(w) + "x" + std::to_string(h) + ")");
    }
    ctx_    = &ctx;
    width_  = w;
    height_ = h;

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!hinst) gl_throw_winapi("GetModuleHandleW (GLWindow)");

    // ---- 1. Register window class (idempotent). -------------
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = reinterpret_cast<WNDPROC>(&GLWindow::wnd_proc_thunk);
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;                // we paint with GL
    wc.lpszClassName = kWndClassName;
    if (RegisterClassExW(&wc) == 0) {
        const DWORD err = GetLastError();
        if (err != 1410 /* ERROR_CLASS_ALREADY_EXISTS */) {
            gl_throw_winapi("RegisterClassExW (GLWindow)");
        }
    }

    // ---- 2. Compute full window rect for desired client size. -
    RECT rc = { 0, 0, w, h };
    const DWORD style    = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    const DWORD ex_style = 0;
    if (!AdjustWindowRectEx(&rc, style, FALSE, ex_style)) {
        gl_throw_winapi("AdjustWindowRectEx");
    }
    const int full_w = rc.right - rc.left;
    const int full_h = rc.bottom - rc.top;

    // ---- 3. Create visible window. --------------------------
    const std::wstring wtitle = utf8_to_utf16(title);
    hwnd_ = reinterpret_cast<HWND_t>(CreateWindowExW(
        ex_style,
        kWndClassName,
        wtitle.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        full_w, full_h,
        nullptr, nullptr, hinst,
        this));                       // <-- passed to WM_NCCREATE
    if (!hwnd_) gl_throw_winapi("CreateWindowExW (GLWindow)");

    hdc_ = reinterpret_cast<HDC_t>(
        GetDC(reinterpret_cast<HWND>(hwnd_)));
    if (!hdc_) gl_throw_winapi("GetDC (GLWindow)");

    // ---- 4. Match pixel format to GLContext's. --------------
    PIXELFORMATDESCRIPTOR pfd = make_legacy_pfd();
    const int pf = ChoosePixelFormat(reinterpret_cast<HDC>(hdc_), &pfd);
    if (pf == 0) gl_throw_winapi("ChoosePixelFormat (GLWindow)");
    if (!SetPixelFormat(reinterpret_cast<HDC>(hdc_), pf, &pfd)) {
        gl_throw_winapi("SetPixelFormat (GLWindow)");
    }

    // ---- 5. Re-bind the GLContext's HGLRC to OUR hdc. -------
    // Extract HGLRC from GLContext via a narrow public accessor
    // we must add. For now, go through a thin trick: we know
    // that the current context is ctx_'s, so wglGetCurrentContext
    // returns it.
    HGLRC hglrc = wglGetCurrentContext();
    if (!hglrc) {
        throw std::runtime_error(
            "GLWindow: no current GL context at time of window creation. "
            "GLContext must be constructed before GLWindow.");
    }
    if (!wglMakeCurrent(reinterpret_cast<HDC>(hdc_), hglrc)) {
        gl_throw_winapi("wglMakeCurrent (GLWindow -> new HDC)");
    }

    // ---- 6. Show the window. --------------------------------
    ShowWindow(reinterpret_cast<HWND>(hwnd_), SW_SHOWNORMAL);
    UpdateWindow(reinterpret_cast<HWND>(hwnd_));
}

// =============================================================
// Dtor
// =============================================================
GLWindow::~GLWindow() {
    if (hdc_ && hwnd_) {
        ReleaseDC(reinterpret_cast<HWND>(hwnd_),
                  reinterpret_cast<HDC>(hdc_));
        hdc_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(reinterpret_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    // Do NOT UnregisterClass — see gl_context.cpp for rationale.
    // Do NOT delete HGLRC — we don't own it; GLContext does.
}

// =============================================================
// Message pump
// =============================================================
void GLWindow::poll_events() {
    MSG msg;
    // Drain all pending messages for the current thread, non-blocking.
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

std::vector<WinEvent> GLWindow::drain_events() {
    std::vector<WinEvent> out;
    out.swap(queue_);
    return out;
}

void GLWindow::swap_buffers() {
    if (!hdc_) return;
    SwapBuffers(reinterpret_cast<HDC>(hdc_));
}
