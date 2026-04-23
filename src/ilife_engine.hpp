#pragma once

// =============================================================
// ilife_engine.hpp — abstract backend interface for Conway's
// Game of Life GPU solver.
//
// Introduced in B4. Extended in B6.3 with per-cell access.
// =============================================================

#include <cstdint>
#include <string>
#include <vector>

class ILifeEngine {
public:
    virtual ~ILifeEngine() = default;

    // ---- State initialization -------------------------------
    virtual void randomize(std::uint32_t seed, double p = 0.30) = 0;
    virtual void upload_host(const std::vector<std::uint8_t>& pattern) = 0;

    // ---- Simulation -----------------------------------------
    virtual void step() = 0;
    virtual void read_host(std::vector<std::uint8_t>& out) = 0;

    // ---- Per-cell access (added in B6.3) --------------------
    // get_cell: non-const because GPU backends require a
    // download (mutates CPU-side cache).
    virtual bool get_cell(int x, int y) = 0;
    virtual void paint_cell(int x, int y, bool alive) = 0;

    // ---- Introspection --------------------------------------
    virtual int           width()      const noexcept = 0;
    virtual int           height()     const noexcept = 0;
    virtual std::uint64_t generation() const noexcept = 0;
    virtual const char*   backend_name() const noexcept = 0;

    // ---- Render interop hook --------------------------------
    virtual unsigned int  current_gl_buffer() const noexcept = 0;

protected:
    ILifeEngine()                              = default;
    ILifeEngine(const ILifeEngine&)            = delete;
    ILifeEngine& operator=(const ILifeEngine&) = delete;
    ILifeEngine(ILifeEngine&&)                 = delete;
    ILifeEngine& operator=(ILifeEngine&&)      = delete;
};
