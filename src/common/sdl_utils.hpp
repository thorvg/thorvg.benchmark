#pragma once

#include <SDL.h>
#ifdef __APPLE__
#include <SDL2/SDL_metal.h>
#endif
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace bench {

inline bool verify_gl33_core_context(const char *adapter_name) {
  int major = 0;
  int minor = 0;
  int profile = 0;
  int red = 0;
  int green = 0;
  int blue = 0;
  int alpha = 0;
  int depth = 0;
  int stencil = 0;
  int double_buffer = 0;
  int multisample_buffers = 0;
  int multisample_samples = 0;

  const auto read = [](SDL_GLattr attribute, int &value) {
    return SDL_GL_GetAttribute(attribute, &value) == 0;
  };
  const bool read_ok =
      read(SDL_GL_CONTEXT_MAJOR_VERSION, major) &&
      read(SDL_GL_CONTEXT_MINOR_VERSION, minor) &&
      read(SDL_GL_CONTEXT_PROFILE_MASK, profile) &&
      read(SDL_GL_RED_SIZE, red) && read(SDL_GL_GREEN_SIZE, green) &&
      read(SDL_GL_BLUE_SIZE, blue) && read(SDL_GL_ALPHA_SIZE, alpha) &&
      read(SDL_GL_DEPTH_SIZE, depth) && read(SDL_GL_STENCIL_SIZE, stencil) &&
      read(SDL_GL_DOUBLEBUFFER, double_buffer) &&
      read(SDL_GL_MULTISAMPLEBUFFERS, multisample_buffers) &&
      read(SDL_GL_MULTISAMPLESAMPLES, multisample_samples);
  if (!read_ok) {
    std::cerr << adapter_name << " failed to query the GL context: "
              << SDL_GetError() << "\n";
    return false;
  }

  const bool version_ok = major > 3 || (major == 3 && minor >= 3);
  const bool profile_ok = (profile & SDL_GL_CONTEXT_PROFILE_CORE) != 0;
  const bool format_ok = red == 8 && green == 8 && blue == 8 && alpha == 8;
  // Some macOS pixel formats promote requested depth24 to depth32.
  const bool attachments_ok = depth >= 24 && stencil >= 8;
  const bool buffering_ok = double_buffer == 1;
  const bool msaa_off = multisample_buffers == 0 || multisample_samples == 0;

  std::cout << adapter_name << " GL context: " << major << "." << minor
            << " core=" << (profile_ok ? "yes" : "no") << " RGBA=" << red
            << green << blue << alpha << " depth=" << depth
            << " stencil=" << stencil << " samples=" << multisample_samples
            << "\n";
  if (!version_ok || !profile_ok || !format_ok || !attachments_ok ||
      !buffering_ok || !msaa_off) {
    std::cerr << adapter_name
              << " requires GL >=3.3 core, RGBA8, depth24/stencil8, double "
                 "buffering, and MSAA disabled\n";
    return false;
  }
  return true;
}

// Correctness captures intentionally use the simple, lossless PPM format so
// every adapter can emit reference images without adding an image-codec
// dependency to the benchmark binaries.
inline bool write_ppm_rgba(const std::string &path, const uint8_t *pixels,
                           uint32_t width, uint32_t height,
                           size_t row_stride, bool flip_y = false) {
  if (!pixels || width == 0 || height == 0 || row_stride < width * 4u) {
    return false;
  }

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << "P6\n" << width << " " << height << "\n255\n";

  std::vector<uint8_t> rgb(static_cast<size_t>(width) * 3u);
  for (uint32_t output_y = 0; output_y < height; ++output_y) {
    const uint32_t source_y = flip_y ? height - 1u - output_y : output_y;
    const uint8_t *row = pixels + static_cast<size_t>(source_y) * row_stride;
    for (uint32_t x = 0; x < width; ++x) {
      rgb[static_cast<size_t>(x) * 3u] = row[static_cast<size_t>(x) * 4u];
      rgb[static_cast<size_t>(x) * 3u + 1u] =
          row[static_cast<size_t>(x) * 4u + 1u];
      rgb[static_cast<size_t>(x) * 3u + 2u] =
          row[static_cast<size_t>(x) * 4u + 2u];
    }
    output.write(reinterpret_cast<const char *>(rgb.data()),
                 static_cast<std::streamsize>(rgb.size()));
  }
  return output.good();
}

inline bool write_ppm_sdl_surface(const std::string &path,
                                  SDL_Surface *surface) {
  if (!surface || !surface->format || surface->w <= 0 || surface->h <= 0 ||
      surface->format->BytesPerPixel == 0 ||
      surface->format->BytesPerPixel > sizeof(uint32_t)) {
    return false;
  }
  if (SDL_LockSurface(surface) != 0) {
    std::cerr << "SDL_LockSurface failed: " << SDL_GetError() << "\n";
    return false;
  }

  std::vector<uint8_t> rgba(static_cast<size_t>(surface->w) *
                            static_cast<size_t>(surface->h) * 4u);
  const auto *source = static_cast<const uint8_t *>(surface->pixels);
  for (int y = 0; y < surface->h; ++y) {
    const uint8_t *row = source + static_cast<size_t>(y) * surface->pitch;
    for (int x = 0; x < surface->w; ++x) {
      uint32_t packed = 0;
      std::memcpy(&packed,
                  row + static_cast<size_t>(x) * surface->format->BytesPerPixel,
                  surface->format->BytesPerPixel);
      Uint8 r = 0;
      Uint8 g = 0;
      Uint8 b = 0;
      Uint8 a = 0;
      SDL_GetRGBA(packed, surface->format, &r, &g, &b, &a);
      const size_t offset =
          (static_cast<size_t>(y) * surface->w + static_cast<size_t>(x)) * 4u;
      rgba[offset] = r;
      rgba[offset + 1u] = g;
      rgba[offset + 2u] = b;
      rgba[offset + 3u] = a;
    }
  }
  SDL_UnlockSurface(surface);

  return write_ppm_rgba(path, rgba.data(), static_cast<uint32_t>(surface->w),
                        static_cast<uint32_t>(surface->h),
                        static_cast<size_t>(surface->w) * 4u);
}

/// Result of HiDPI window adjustment
struct WindowDimensions {
  int drawable_w;
  int drawable_h;
};

/// Helper to get drawable size based on context
inline void get_drawable_size(SDL_Window *window, SDL_Renderer *renderer,
                               bool use_gl_drawable, int *w, int *h) {
  if (use_gl_drawable) {
    SDL_GL_GetDrawableSize(window, w, h);
    return;
  }

#ifdef __APPLE__
  // On macOS, SDL_GetRendererOutputSize doesn't always report HiDPI correctly.
  // Use SDL_Metal_GetDrawableSize which works reliably for Metal-backed windows.
  SDL_Metal_GetDrawableSize(window, w, h);
  // If Metal returns 0 (not a Metal window), fall back to renderer
  if (*w > 0 && *h > 0) {
    return;
  }
#endif

  if (renderer) {
    SDL_GetRendererOutputSize(renderer, w, h);
  } else {
    // Fallback: try to get renderer from window
    SDL_Renderer *win_renderer = SDL_GetRenderer(window);
    if (win_renderer) {
      SDL_GetRendererOutputSize(win_renderer, w, h);
    } else {
      // Last resort: use window size (no HiDPI detection possible)
      SDL_GetWindowSize(window, w, h);
    }
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
/// @param use_gl_drawable If true, use SDL_GL_GetDrawableSize (for OpenGL windows)
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

    // Verify the new drawable size
    get_drawable_size(window, renderer, use_gl_drawable, &drawable_w, &drawable_h);

    std::cout << "Final drawable size: " << drawable_w << "x" << drawable_h << "\n";
  }

  result.drawable_w = drawable_w;
  result.drawable_h = drawable_h;
  return result;
}

} // namespace bench
