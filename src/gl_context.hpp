#pragma once

// =============================================================
// gl_context.hpp — Headless OpenGL 4.6 Core Profile context
//                  via WGL (Windows GL binding) + GLAD 2 loader.
//
// Semantics:
//   * Creates an invisible top-level window (Technique A from Evans 2023).
//   * Bootstraps a Core Profile context using WGL_ARB_create_context.
//   * Loads GL function pointers via GLAD 2.
//   * Optionally registers a debug callback (enabled via GL_DEBUG=1 env).
//
// Lifecycle (RAII):
//   GLContext ctx(debug_flag);   // constructs ready-to-use context
//   // ... perform GL calls ...
//   // ~GLContext() at scope exit releases all resources.
//
// Thread-safety:
//   A GL context is bound to the thread that called wglMakeCurrent.
//   Construct and use on the same thread. Not movable, not copyable.
// =============================================================

#include <string>
#include <stdexcept>

// Forward-declarations to avoid dragging <windows.h> into every TU.
// The concrete PODs are platform-native handles; we treat them opaquely here.
struct HWND__;       using HWND_t  = HWND__*;
struct HDC__;        using HDC_t   = HDC__*;
struct HGLRC__;      using HGLRC_t = HGLRC__*;
struct HINSTANCE__;  using HINSTANCE_t = HINSTANCE__*;

class GLContext {
public:
    // Construct and fully initialize the context.
    //  @param enable_debug  If true, request WGL_CONTEXT_DEBUG_BIT_ARB
    //                       and install glDebugMessageCallback.
    //  @throws std::runtime_error on any failure (with descriptive message).
    explicit GLContext(bool enable_debug);

    // Destructor: releases context, DC, window, window class in reverse order.
    ~GLContext();

    // Non-copyable, non-movable (exclusive ownership of OS resources).
    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;
    GLContext(GLContext&&) = delete;
    GLContext& operator=(GLContext&&) = delete;

    // Runtime introspection (filled in constructor).
    const std::string& gl_version()  const noexcept { return gl_version_; }
    const std::string& gl_renderer() const noexcept { return gl_renderer_; }
    const std::string& gl_vendor()   const noexcept { return gl_vendor_; }

    // Returns true if WGL_CONTEXT_DEBUG_BIT_ARB was requested and honored.
    bool debug_enabled() const noexcept { return debug_enabled_; }

private:
    // OS resources (ordered by construction lifetime).
    HINSTANCE_t hinstance_ = nullptr;
    unsigned short class_atom_ = 0;   // 0 means "not registered"
    HWND_t  hwnd_  = nullptr;
    HDC_t   hdc_   = nullptr;
    HGLRC_t hglrc_ = nullptr;         // Final Core Profile context.

    // Introspection strings (captured at init).
    std::string gl_version_;
    std::string gl_renderer_;
    std::string gl_vendor_;

    bool debug_enabled_ = false;

    // Constant used for window class name (also in .cpp).
    static constexpr const wchar_t* kWindowClassName = L"LifeGLHeadlessCtxClass_v1";
};

// -------------------------------------------------------------
// Free helper: fatal-throw formatter.
// Centralizes "GetLastError() + message" to a runtime_error.
// Defined in gl_context.cpp.
// -------------------------------------------------------------
[[noreturn]] void gl_throw_winapi(const char* where);
