// =============================================================
// main.cpp — production entry point for LifeGL viewer.
// Refactor phase B6.4: fixed broken arg-parser bracketing.
// =============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "life_app.hpp"

#include <cstdio>
#include <cstdint>
#include <exception>
#include <string>

static bool parse_int(const std::string& s, const char* key, int& out) {
    const std::string k = std::string("--") + key + "=";
    if (s.rfind(k, 0) != 0) return false;
    try { out = std::stoi(s.substr(k.size())); return true; }
    catch (...) { return false; }
}
static bool parse_uint32(const std::string& s, const char* key,
                         std::uint32_t& out) {
    const std::string k = std::string("--") + key + "=";
    if (s.rfind(k, 0) != 0) return false;
    try {
        const auto v = std::stoul(s.substr(k.size()), nullptr, 0);
        out = static_cast<std::uint32_t>(v);
        return true;
    } catch (...) { return false; }
}
static bool parse_double(const std::string& s, const char* key, double& out) {
    const std::string k = std::string("--") + key + "=";
    if (s.rfind(k, 0) != 0) return false;
    try { out = std::stod(s.substr(k.size())); return true; }
    catch (...) { return false; }
}

static void print_help() {
    std::printf(
        "LifeGL - Conway's Life on GPU (OpenGL 4.6 compute).\n"
        "Usage: life_gl.exe [options]\n"
        "  --grid_w=N         Grid width in cells  (default 512)\n"
        "  --grid_h=N         Grid height in cells (default 512)\n"
        "  --win_w=N          Window width  in px  (default 1280)\n"
        "  --win_h=N          Window height in px  (default 960)\n"
        "  --ticks_per_sec=F  Simulation rate      (default 30.0)\n"
        "  --seed=U           RNG seed (0 = empty) (default 0xC0FFEE)\n"
        "  --p=F              Bernoulli fill prob. (default 0.28)\n"
        "  --paused           Start paused\n"
        "  --help             Show this help and exit\n");
}

int main(int argc, char** argv) {
    LifeAppConfig cfg;
    cfg.grid_w        = 512;
    cfg.grid_h        = 512;
    cfg.win_w         = 1280;
    cfg.win_h         = 960;
    cfg.ticks_per_sec = 30.0;
    cfg.start_paused  = false;
    cfg.random_seed   = 0xC0FFEEu;
    cfg.fill_prob     = 0.28;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { print_help(); return 0; }
        if (a == "--paused") { cfg.start_paused = true; continue; }
        if (parse_int   (a, "grid_w",        cfg.grid_w))        continue;
        if (parse_int   (a, "grid_h",        cfg.grid_h))        continue;
        if (parse_int   (a, "win_w",         cfg.win_w))         continue;
        if (parse_int   (a, "win_h",         cfg.win_h))         continue;
        if (parse_double(a, "ticks_per_sec", cfg.ticks_per_sec)) continue;
        if (parse_uint32(a, "seed",          cfg.random_seed))   continue;
        if (parse_double(a, "p",             cfg.fill_prob))     continue;
        std::fprintf(stderr, "[main] WARN: unknown arg '%s'\n", a.c_str());
    }

    try {
        LifeApp app(cfg);
        return app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[main] FATAL: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[main] FATAL: unknown exception\n");
        return 1;
    }
}
