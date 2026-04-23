#pragma once

// =============================================================
// gl_compute_engine.hpp  —  Conway's Game of Life GPU solver.
// Backend: OpenGL 4.6 Compute Shader + std430 SSBO ping-pong.
//
// Renamed from former LifeEngine / life_engine.hpp in refactor
// phase B3. In phase B4, adapted to implement ILifeEngine.
// All simulation semantics preserved bit-for-bit.
//
// Lifecycle: see ilife_engine.hpp for the contract.
//
// Rationale for PIMPL-free header:
//   * No third-party types (GLuint, etc.) leak into the header;
//     we use cstdint types only. glad/gl.h is included ONLY
//     in the implementation TU.
// =============================================================

#include "ilife_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GLComputeEngine final : public ILifeEngine {
public:
    // -------- Construction / destruction ---------------------

    // Builds the engine on the currently-bound GL 4.6 Core context.
    //
    // Parameters:
    //   width, height  : grid dimensions in cells; must be >= 1,
    //                    product must not exceed 2^28 (sanity cap).
    //   kernel_path    : filesystem path to GLSL compute shader
    //                    source (typically "kernels/life.comp").
    //
    // Throws std::invalid_argument on bad dims.
    // Throws std::runtime_error on file-I/O, shader compile,
    // program link, or GL buffer allocation failure.
    GLComputeEngine(int width, int height, const std::string& kernel_path);

    // Deletes the GL program and both SSBOs.
    // Requires the original GL context to be current.
    ~GLComputeEngine() override;

    // Copy/move explicitly forbidden by the base (ILifeEngine).

    // -------- ILifeEngine implementation ---------------------

    void          randomize(std::uint32_t seed, double p = 0.30) override;
    void          upload_host(const std::vector<std::uint8_t>& pattern) override;
    void          step() override;
    void          read_host(std::vector<std::uint8_t>& out) override;

    int           width()      const noexcept override { return w_; }
    int           height()     const noexcept override { return h_; }
    std::uint64_t generation() const noexcept override { return generation_; }

    const char*   backend_name() const noexcept override { return "glcompute"; }

    bool get_cell(int x, int y) override ;
    void paint_cell(int x, int y, bool alive) override;

    // Native GL handle of the SSBO holding the current state.
    unsigned int  current_gl_buffer() const noexcept override {
        return ssbo_[current_];
    }

private:
    // Grid dimensions (cells).
    int           w_ = 0;
    int           h_ = 0;

    // Byte size of one SSBO: w*h * sizeof(uint32_t).
    std::size_t   byte_size_ = 0;

    // Dispatch dimensions: ceil(w / 16), ceil(h / 16).
    unsigned int  wg_x_ = 0;
    unsigned int  wg_y_ = 0;

    // GL program object for the compute shader.
    unsigned int  prog_ = 0;

    // Double-buffered SSBOs.
    unsigned int  ssbo_[2] = {0, 0};

    // Index of the buffer holding the current state (0 or 1).
    int           current_ = 0;

    // Generation counter; incremented by step().
    std::uint64_t generation_ = 0;

    // Internal helpers. Defined in gl_compute_engine.cpp.
    static std::string  load_text_file(const std::string& path);
    static unsigned int compile_compute_shader(const std::string& src);
    static unsigned int link_compute_program(unsigned int shader);
};
