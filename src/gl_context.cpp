// =============================================================
// gl_context.cpp
// Implementation of GLContext (see gl_context.hpp).
//
// Fixes vs previous revision (B6.3 patch):
//   * Bug#1: removed double wglDeleteContext + make-current-NULL ordering.
//   * Bug#2: CreateWindowExW now uses kWindowClassName (string), not atom.
//     This is robust against RegisterClassExW returning 0 on the 2nd
//     instance (ERROR_CLASS_ALREADY_EXISTS path).
//   * Bug#3: never FreeLibrary(opengl32.dll). That library is process-wide
//     and owned by the WGL driver hooks; unloading corrupts all subsequent
//     GL context creation in the same process.
// =============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wingdi.h>

#include <glad/gl.h>

#include "gl_context.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

HMODULE g_opengl32_module = nullptr;

void* gl_composite_loader(const char* name) {
    PROC p = wglGetProcAddress(name);
    const auto ip = reinterpret_cast<intptr_t>(p);
    if (ip != 0 && ip != 1 && ip != 2 && ip != 3 && ip != -1) {
        return reinterpret_cast<void*>(p);
    }
    if (!g_opengl32_module) {
        g_opengl32_module = LoadLibraryA("opengl32.dll");
    }
    if (!g_opengl32_module) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(g_opengl32_module, name));
}

extern "C" {
    static GLADapiproc gl_composite_loader_glad(const char* name) {
        return reinterpret_cast<GLADapiproc>(gl_composite_loader(name));
    }
}

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB               0x2093
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB                 0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x00000002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

using PFNWGLCREATECONTEXTATTRIBSARBPROC =
    HGLRC (WINAPI*)(HDC, HGLRC, const int*);

#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT                  0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS      0x8242
#endif

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

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

const char* dbg_source_str(GLenum src) {
    switch (src) {
        case GL_DEBUG_SOURCE_API:             return "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   return "WINSYS";
        case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER";
        case GL_DEBUG_SOURCE_THIRD_PARTY:     return "3RD";
        case GL_DEBUG_SOURCE_APPLICATION:     return "APP";
        case GL_DEBUG_SOURCE_OTHER:           return "OTHER";
        default:                              return "?";
    }
}
const char* dbg_type_str(GLenum t) {
    switch (t) {
        case GL_DEBUG_TYPE_ERROR:               return "ERROR";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  return "UB";
        case GL_DEBUG_TYPE_PORTABILITY:         return "PORTABILITY";
        case GL_DEBUG_TYPE_PERFORMANCE:         return "PERF";
        case GL_DEBUG_TYPE_MARKER:              return "MARKER";
        case GL_DEBUG_TYPE_PUSH_GROUP:          return "PUSH";
        case GL_DEBUG_TYPE_POP_GROUP:           return "POP";
        case GL_DEBUG_TYPE_OTHER:               return "OTHER";
        default:                                return "?";
    }
}
const char* dbg_sev_str(GLenum s) {
    switch (s) {
        case GL_DEBUG_SEVERITY_HIGH:         return "HIGH";
        case GL_DEBUG_SEVERITY_MEDIUM:       return "MED";
        case GL_DEBUG_SEVERITY_LOW:          return "LOW";
        case GL_DEBUG_SEVERITY_NOTIFICATION: return "NOTE";
        default:                             return "?";
    }
}

void APIENTRY gl_debug_callback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei /*length*/, const GLchar* message, const void* /*userParam*/)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::cerr << "[GL-DEBUG] "
              << "src="  << dbg_source_str(source)
              << " type=" << dbg_type_str(type)
              << " sev="  << dbg_sev_str(severity)
              << " id="   << id
              << " : "    << (message ? message : "(null)")
              << std::endl;
    if (severity == GL_DEBUG_SEVERITY_HIGH && type == GL_DEBUG_TYPE_ERROR) {
        std::cerr << "[GL-DEBUG] HIGH severity error; aborting." << std::endl;
        std::abort();
    }
}

// ------------------------------------------------------------------
// Bootstrap ARB: uses class NAME (LPCWSTR), NOT atom.
// This is robust across multiple GLContext instances: on the 2nd
// instance, RegisterClassExW may return 0 with ERROR_CLASS_ALREADY_EXISTS,
// but the class is still registered and CreateWindowExW(..., className, ...)
// works correctly.
// ------------------------------------------------------------------
PFNWGLCREATECONTEXTATTRIBSARBPROC load_wgl_arb_functions(
    HINSTANCE hinstance, LPCWSTR className)
{
    HWND dummy_hwnd = CreateWindowExW(
        0, className, L"dummy",
        WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, hinstance, nullptr);
    if (!dummy_hwnd) gl_throw_winapi("CreateWindowExW (dummy)");

    HDC dummy_hdc = GetDC(dummy_hwnd);
    if (!dummy_hdc) {
        DestroyWindow(dummy_hwnd);
        gl_throw_winapi("GetDC (dummy)");
    }

    PIXELFORMATDESCRIPTOR pfd = make_legacy_pfd();
    int pf = ChoosePixelFormat(dummy_hdc, &pfd);
    if (pf == 0) {
        ReleaseDC(dummy_hwnd, dummy_hdc);
        DestroyWindow(dummy_hwnd);
        gl_throw_winapi("ChoosePixelFormat (dummy)");
    }
    if (!SetPixelFormat(dummy_hdc, pf, &pfd)) {
        ReleaseDC(dummy_hwnd, dummy_hdc);
        DestroyWindow(dummy_hwnd);
        gl_throw_winapi("SetPixelFormat (dummy)");
    }

    HGLRC dummy_ctx = wglCreateContext(dummy_hdc);
    if (!dummy_ctx) {
        ReleaseDC(dummy_hwnd, dummy_hdc);
        DestroyWindow(dummy_hwnd);
        gl_throw_winapi("wglCreateContext (dummy)");
    }
    if (!wglMakeCurrent(dummy_hdc, dummy_ctx)) {
        wglDeleteContext(dummy_ctx);
        ReleaseDC(dummy_hwnd, dummy_hdc);
        DestroyWindow(dummy_hwnd);
        gl_throw_winapi("wglMakeCurrent (dummy)");
    }

    auto fn = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummy_ctx);
    ReleaseDC(dummy_hwnd, dummy_hdc);
    DestroyWindow(dummy_hwnd);

    if (!fn) {
        throw std::runtime_error(
            "wglGetProcAddress(\"wglCreateContextAttribsARB\") returned null. "
            "Driver does not support WGL_ARB_create_context.");
    }
    return fn;
}

} // namespace

[[noreturn]] void gl_throw_winapi(const char* where) {
    DWORD code = GetLastError();
    LPWSTR buf = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

    std::ostringstream os;
    os << "WinAPI failure at [" << where << "]: code=" << code;
    if (buf) {
        int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string msg(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, msg.data(), n, nullptr, nullptr);
            os << " (" << msg << ")";
        }
        LocalFree(buf);
    }
    throw std::runtime_error(os.str());
}

GLContext::GLContext(bool enable_debug) {
    hinstance_ = reinterpret_cast<HINSTANCE_t>(GetModuleHandleW(nullptr));
    if (!hinstance_) gl_throw_winapi("GetModuleHandleW");

    // ---- Step 1: Register window class (idempotent). ----
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = DummyWndProc;
    wc.hInstance     = reinterpret_cast<HINSTANCE>(hinstance_);
    wc.lpszClassName = kWindowClassName;
    ATOM atom = RegisterClassExW(&wc);
    if (atom == 0) {
        DWORD err = GetLastError();
        if (err != 1410 /* ERROR_CLASS_ALREADY_EXISTS */) {
            gl_throw_winapi("RegisterClassExW");
        }
        // Class already registered from prior instance — that's OK.
    }
    class_atom_ = static_cast<unsigned short>(atom);

    // ---- Step 2: Bootstrap ARB function (use class NAME, not atom). ----
    auto wglCreateContextAttribsARB = load_wgl_arb_functions(
        reinterpret_cast<HINSTANCE>(hinstance_), kWindowClassName);

    // ---- Step 3: Create real invisible window. ----
    hwnd_ = reinterpret_cast<HWND_t>(CreateWindowExW(
        WS_EX_NOACTIVATE,
        kWindowClassName,
        L"LifeGL-headless",
        WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr,
        reinterpret_cast<HINSTANCE>(hinstance_), nullptr));
    if (!hwnd_) gl_throw_winapi("CreateWindowExW (real)");

    hdc_ = reinterpret_cast<HDC_t>(GetDC(reinterpret_cast<HWND>(hwnd_)));
    if (!hdc_) gl_throw_winapi("GetDC (real)");

    // ---- Step 4: Set pixel format. ----
    PIXELFORMATDESCRIPTOR pfd = make_legacy_pfd();
    int pf = ChoosePixelFormat(reinterpret_cast<HDC>(hdc_), &pfd);
    if (pf == 0) gl_throw_winapi("ChoosePixelFormat (real)");
    if (!SetPixelFormat(reinterpret_cast<HDC>(hdc_), pf, &pfd))
        gl_throw_winapi("SetPixelFormat (real)");

    // ---- Step 5: Create Core Profile 4.6 context. ----
    int profile_flags = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    int context_flags = WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
    if (enable_debug) context_flags |= WGL_CONTEXT_DEBUG_BIT_ARB;

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,
        WGL_CONTEXT_PROFILE_MASK_ARB,  profile_flags,
        WGL_CONTEXT_FLAGS_ARB,         context_flags,
        0
    };

    hglrc_ = reinterpret_cast<HGLRC_t>(wglCreateContextAttribsARB(
        reinterpret_cast<HDC>(hdc_), nullptr, attribs));
    if (!hglrc_) {
        throw std::runtime_error(
            "wglCreateContextAttribsARB failed: GL 4.6 Core not available.");
    }

    // ---- Step 6: Make current. ----
    if (!wglMakeCurrent(reinterpret_cast<HDC>(hdc_),
                        reinterpret_cast<HGLRC>(hglrc_))) {
        gl_throw_winapi("wglMakeCurrent (core)");
    }

    // ---- Step 7: GLAD load. ----
    int glad_ver = gladLoadGL(gl_composite_loader_glad);
    if (glad_ver == 0) {
        auto probe = reinterpret_cast<const GLubyte* (APIENTRY*)(GLenum)>(
            gl_composite_loader("glGetString"));
        const GLubyte* ver = probe ? probe(GL_VERSION) : nullptr;
        std::ostringstream os;
        os << "gladLoadGL failed. Direct probe of glGetString: "
           << (probe ? "resolved" : "NULL") << ". GL_VERSION: "
           << (ver ? reinterpret_cast<const char*>(ver) : "<null>");
        throw std::runtime_error(os.str());
    }

    // ---- Step 8: Introspection. ----
    auto safe_str = [](const GLubyte* s) -> std::string {
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("<null>");
    };
    gl_version_  = safe_str(glGetString(GL_VERSION));
    gl_renderer_ = safe_str(glGetString(GL_RENDERER));
    gl_vendor_   = safe_str(glGetString(GL_VENDOR));

    // ---- Step 9: Debug callback. ----
    if (enable_debug) {
        GLint ctx_flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &ctx_flags);
        if ((ctx_flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(gl_debug_callback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE,
                                  0, nullptr, GL_TRUE);
            debug_enabled_ = true;
        } else {
            std::cerr << "[GL-WARN] Debug bit requested but not honored by driver."
                      << std::endl;
        }
    }
}

// =============================================================
// GLContext::~GLContext — RAII cleanup (FIXED).
//
// Correct WGL teardown sequence per Khronos WGL spec §5.1:
//   (a) wglMakeCurrent(NULL, NULL)  -> detach context from thread.
//   (b) wglDeleteContext(hglrc)     -> destroy GL context.
//   (c) ReleaseDC(hwnd, hdc)        -> release DC handle.
//   (d) DestroyWindow(hwnd)         -> destroy window.
//
// CRITICAL: never FreeLibrary(opengl32.dll). That module is owned by
// the process-wide WGL hook chain; unloading corrupts all subsequent
// GL context creation.
//
// CRITICAL: never UnregisterClassW here. Class atoms are process-
// scoped and cleaned up on process exit. Unregistering while other
// live windows of the same class exist (e.g. GLWindow) fails.
// =============================================================
GLContext::~GLContext() {
    // (a) Detach current context, if any, from this thread.
    //     We check hglrc_ first: if it was never created, nothing to detach.
    if (hglrc_) {
        // Only detach if *our* context is actually current on this thread.
        HGLRC current = wglGetCurrentContext();
        if (current == reinterpret_cast<HGLRC>(hglrc_)) {
            wglMakeCurrent(nullptr, nullptr);
        }
        // (b) Delete the context exactly once.
        wglDeleteContext(reinterpret_cast<HGLRC>(hglrc_));
        hglrc_ = nullptr;
    }

    // (c) Release DC.
    if (hdc_ && hwnd_) {
        ReleaseDC(reinterpret_cast<HWND>(hwnd_), reinterpret_cast<HDC>(hdc_));
        hdc_ = nullptr;
    }

    // (d) Destroy window.
    if (hwnd_) {
        DestroyWindow(reinterpret_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }

    // Window class deliberately NOT unregistered (see comment above).
    class_atom_ = 0;

    // opengl32.dll deliberately NOT unloaded (see comment above).
    // g_opengl32_module is a process-wide cached handle and must
    // outlive every GLContext instance.
}
