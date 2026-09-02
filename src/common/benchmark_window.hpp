#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

namespace bench {

// Minimal interface for a benchmarkable SDL "window" that can be driven by the
// shared run loop (warmup/measured frames, event pumping, timing, output).
struct BenchmarkWindow {
  virtual ~BenchmarkWindow() = default;

  // Prepare the initial scene/resources. Called once before timing starts.
  virtual bool ready() = 0;

  // Update the scene for a given deterministic frame index.
  virtual bool update(uint32_t frame_index) = 0;

  // Encode and submit rendering for the current scene. GPU completion belongs
  // in finish_gpu(), after refresh() has presented the frame.
  virtual bool draw() = 0;

  // Present the rendered result to the window.
  virtual void refresh() = 0;

  // Wait until GPU work for the just-presented frame has completed. CPU
  // backends may keep the default no-op implementation.
  virtual bool finish_gpu() { return true; }

  // Capture the currently rendered frame. Adapters that support capture
  // should write an image to `path`; unsupported adapters return false.
  virtual bool capture(const std::string &path) {
    (void)path;
    return false;
  }

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

  // Reproducibility/comparability metadata. Engine adapters override the
  // fields they can report; stable defaults keep older adapters source
  // compatible.
  virtual const char *engine_version() const { return "unknown"; }
  virtual const char *engine_revision() const { return "unknown"; }
  virtual const char *scene_model() const { return "unknown"; }
  virtual const char *graphics_api() const { return backend_title(); }
  virtual const char *gpu_device() const { return "unknown"; }
  virtual const char *gpu_vendor() const { return "unknown"; }
  virtual const char *gpu_driver() const { return "unknown"; }
  virtual const char *gpu_completion() const { return "none"; }
  virtual const char *present_mode() const { return "unknown"; }
  virtual const char *pixel_format() const { return "unknown"; }
  // Digest of the exact bytes consumed by an asset-backed workload.
  virtual const char *asset_hash() const { return ""; }
  virtual bool vsync_actual() const { return false; }
  virtual bool vsync_verified() const { return true; }
};

} // namespace bench
