#pragma once

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif
#include <cstdint>
#include <iostream>

namespace bench {

/// Result of HiDPI window adjustment
struct WindowDimensions {
  int drawable_w;
  int drawable_h;
  float scale_x;
  float scale_y;
};

// SDL3 windows are visible by default, but terminal launches on macOS need
// an explicit raise to activate the application and bring the window forward.
inline SDL_Window *create_centered_window(const char *title, int width,
                                         int height, SDL_WindowFlags flags) {
  SDL_Window *window = SDL_CreateWindow(title, width, height, flags);
  if (window) {
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    if (!SDL_RaiseWindow(window)) {
      std::cerr << "Warning: Unable to raise window: " << SDL_GetError() << "\n";
    }
  }
  return window;
}

/// Get physical pixel dimensions for OpenGL, Metal, or software windows.
inline void get_drawable_size(SDL_Window *window, SDL_Renderer *renderer,
                              bool /*use_gl_drawable*/, int *w, int *h) {
  if (renderer && SDL_GetRenderOutputSize(renderer, w, h)) {
    return;
  }
  if (!SDL_GetWindowSizeInPixels(window, w, h)) {
    SDL_GetWindowSize(window, w, h);
  }
}

/// Adjust window size for HiDPI displays to ensure drawable size matches target.
///
/// On HiDPI/Retina displays, the drawable (pixel) size may differ from the
/// window size. This function detects the scale factor and resizes the window
/// so that the actual drawable resolution matches the target.
///
/// @param window The SDL window to adjust
/// @param target_w Target drawable width in pixels
/// @param target_h Target drawable height in pixels
/// @param use_gl_drawable If true, use SDL_GetWindowSizeInPixels (for OpenGL windows)
/// @param renderer Optional SDL_Renderer for CPU backends (pass nullptr for GL)
/// @return The final drawable dimensions and scale factors
inline WindowDimensions adjust_window_for_hidpi(SDL_Window *window,
                                                 uint32_t target_w,
                                                 uint32_t target_h,
                                                 bool use_gl_drawable = false,
                                                 SDL_Renderer *renderer = nullptr) {
  WindowDimensions result{};

  // Get initial window and drawable sizes
  int window_w, window_h;
  SDL_GetWindowSize(window, &window_w, &window_h);

  int drawable_w, drawable_h;
  get_drawable_size(window, renderer, use_gl_drawable, &drawable_w, &drawable_h);

  // Calculate scale factors
  float scale_x = static_cast<float>(drawable_w) / static_cast<float>(window_w);
  float scale_y = static_cast<float>(drawable_h) / static_cast<float>(window_h);

  std::cout << "Initial window size: " << window_w << "x" << window_h << "\n";
  std::cout << "Initial drawable size: " << drawable_w << "x" << drawable_h << "\n";
  std::cout << "DPI scale: " << scale_x << "x" << scale_y << "\n";

  // Check if we need to adjust
  if (static_cast<uint32_t>(drawable_w) != target_w ||
      static_cast<uint32_t>(drawable_h) != target_h) {
    // Calculate the window size needed to achieve target drawable size
    int new_window_w = static_cast<int>(static_cast<float>(target_w) / scale_x);
    int new_window_h = static_cast<int>(static_cast<float>(target_h) / scale_y);

    std::cout << "Adjusting window size to " << new_window_w << "x" << new_window_h
              << " to achieve " << target_w << "x" << target_h << " drawable\n";

    SDL_SetWindowSize(window, new_window_w, new_window_h);

    // Wait for asynchronous resize requests before querying pixel dimensions.
    if (!SDL_SyncWindow(window)) {
      std::cerr << "Warning: Unable to synchronize window resize: " << SDL_GetError() << "\n";
    }

    // Verify the new drawable size
    get_drawable_size(window, renderer, use_gl_drawable, &drawable_w, &drawable_h);

    std::cout << "Final drawable size: " << drawable_w << "x" << drawable_h << "\n";
  }

  result.drawable_w = drawable_w;
  result.drawable_h = drawable_h;
  result.scale_x = scale_x;
  result.scale_y = scale_y;

  return result;
}

} // namespace bench

