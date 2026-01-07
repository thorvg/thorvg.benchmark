#pragma once

#include <SDL2/SDL.h>

#include <cstdint>

namespace bench {

// Minimal interface for a benchmarkable SDL "window" that can be driven by the
// shared run loop (warmup/measured frames, event pumping, timing, output).
struct BenchmarkWindow {
  virtual ~BenchmarkWindow() = default;

  // Prepare the initial scene/resources. Called once before timing starts.
  virtual bool ready() = 0;

  // Update the scene for a given deterministic frame index.
  virtual bool update(uint32_t frame_index) = 0;

  // Render the current scene (and block until rendering is finished if the
  // backend requires explicit sync).
  virtual bool draw() = 0;

  // Present the rendered result to the window.
  virtual void refresh() = 0;

  // Process SDL events and set `running=false` to request exit.
  virtual bool pump_events(bool &running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      }
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        running = false;
      }
    }
    return running;
  }

  // Pixel workload size (drawable/surface) used for metadata.
  virtual uint32_t surface_width() const = 0;
  virtual uint32_t surface_height() const = 0;

  // For output/printing.
  virtual const char *engine_id() const = 0;     // e.g. "skia", "thorvg"
  virtual const char *engine_title() const = 0;  // e.g. "Skia", "ThorVG"
  virtual const char *backend_id() const = 0;    // e.g. "cpu", "gl", "webgpu"
  virtual const char *backend_title() const = 0; // e.g. "CPU", "OpenGL"
};

} // namespace bench

