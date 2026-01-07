#pragma once

// A small, Example.h-inspired SDL + Skia window framework used by rectbench.
// Goal: keep setup/conditions easy to compare across engines (ThorVG/Skia).

#include "benchmark_window.hpp"
#include "sdl_utils.hpp"

#include <SDL2/SDL.h>

// Skia headers
#include "core/SkCanvas.h"
#include "core/SkColorSpace.h"
#include "core/SkColorType.h"
#include "core/SkImageInfo.h"
#include "core/SkSurface.h"

#include "gpu/ganesh/GrBackendSurface.h"
#include "gpu/ganesh/GrDirectContext.h"
#include "gpu/ganesh/SkSurfaceGanesh.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "gpu/ganesh/gl/GrGLDirectContext.h"
#include "gpu/ganesh/gl/GrGLInterface.h"

#define GL_SILENCE_DEPRECATION
#ifndef __APPLE__
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#endif

#include <SDL2/SDL_opengl.h>

#include <cstdint>
#include <iostream>

namespace bench::skiaexam {

struct Example {
  uint32_t elapsed = 0;

  virtual bool content(SkCanvas *canvas, uint32_t w, uint32_t h) = 0;
  virtual bool update(SkCanvas *canvas, uint32_t elapsed) {
    (void)canvas;
    (void)elapsed;
    return false;
  }
  virtual bool draw(SkCanvas *canvas) = 0;

  virtual ~Example() = default;
};

inline bool adjust_window_surface_to_target(SDL_Window *window,
                                            uint32_t target_w,
                                            uint32_t target_h) {
  if (!window) {
    return false;
  }

  // Align surface pixel size to the requested target size on HiDPI displays.
  // We derive scaling from (surface pixels) / (window points).
  for (int attempt = 0; attempt < 4; ++attempt) {
    int window_w = 0;
    int window_h = 0;
    SDL_GetWindowSize(window, &window_w, &window_h);
    if (window_w <= 0 || window_h <= 0) {
      break;
    }

    SDL_Surface *current_surface = SDL_GetWindowSurface(window);
    if (!current_surface) {
      std::cerr << "SDL_GetWindowSurface failed: " << SDL_GetError() << "\n";
      return false;
    }

    const float scale_x =
        static_cast<float>(current_surface->w) / static_cast<float>(window_w);
    const float scale_y =
        static_cast<float>(current_surface->h) / static_cast<float>(window_h);

    if (scale_x <= 0.0f || scale_y <= 0.0f) {
      break;
    }

    if (static_cast<uint32_t>(current_surface->w) == target_w &&
        static_cast<uint32_t>(current_surface->h) == target_h) {
      break;
    }

    const int new_window_w =
        static_cast<int>(static_cast<float>(target_w) / scale_x + 0.5f);
    const int new_window_h =
        static_cast<int>(static_cast<float>(target_h) / scale_y + 0.5f);

    if (new_window_w <= 0 || new_window_h <= 0) {
      break;
    }
    if (new_window_w == window_w && new_window_h == window_h) {
      break;
    }

    SDL_SetWindowSize(window, new_window_w, new_window_h);
  }

  return true;
}

inline bool skia_image_info_from_sdl_surface(const SDL_Surface *surface,
                                             SkImageInfo &out_info) {
  if (!surface || !surface->format) {
    return false;
  }

  if (surface->format->BytesPerPixel != 4) {
    std::cerr << "Unsupported SDL surface format: BytesPerPixel="
              << static_cast<int>(surface->format->BytesPerPixel) << "\n";
    return false;
  }

  int bpp = 0;
  Uint32 rmask = 0;
  Uint32 gmask = 0;
  Uint32 bmask = 0;
  Uint32 amask = 0;
  if (SDL_PixelFormatEnumToMasks(surface->format->format, &bpp, &rmask, &gmask,
                                 &bmask, &amask) == SDL_FALSE) {
    std::cerr << "SDL_PixelFormatEnumToMasks failed for surface format: "
              << surface->format->format << "\n";
    return false;
  }

  if (bpp != 32) {
    std::cerr << "Unsupported SDL surface format: bpp=" << bpp << "\n";
    return false;
  }

  SkColorType color_type = kUnknown_SkColorType;

  // Match common 32-bit packed formats on little-endian systems.
  if (rmask == 0x000000ff && gmask == 0x0000ff00 && bmask == 0x00ff0000) {
    color_type = kRGBA_8888_SkColorType;
  } else if (bmask == 0x000000ff && gmask == 0x0000ff00 &&
             rmask == 0x00ff0000) {
    color_type = kBGRA_8888_SkColorType;
  } else {
    std::cerr << "Unsupported SDL surface channel masks: r=0x" << std::hex
              << rmask << " g=0x" << gmask << " b=0x" << bmask << " a=0x"
              << amask << std::dec << "\n";
    return false;
  }

  // Always use premultiplied alpha to ensure proper alpha blending
  // even on surfaces without an explicit alpha channel.
  // This allows images with transparency to blend correctly with the background.
  const SkAlphaType alpha_type = kPremul_SkAlphaType;

  out_info = SkImageInfo::Make(surface->w, surface->h, color_type, alpha_type,
                               SkColorSpace::MakeSRGB());
  return true;
}

inline GrGLuint current_draw_fbo() {
  GLint fbo = 0;
#ifdef GL_DRAW_FRAMEBUFFER_BINDING
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
#else
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
#endif
  return static_cast<GrGLuint>(fbo);
}

using GLGetFramebufferAttachmentParameterivProc = void (*)(GLenum, GLenum,
                                                           GLenum, GLint *);
using GLBindFramebufferProc = void (*)(GLenum, GLuint);
using GLBindRenderbufferProc = void (*)(GLenum, GLuint);
using GLGetRenderbufferParameterivProc = void (*)(GLenum, GLenum, GLint *);

inline GLGetFramebufferAttachmentParameterivProc
get_framebuffer_attachment_param_iv() {
  static auto fn = reinterpret_cast<GLGetFramebufferAttachmentParameterivProc>(
      SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv"));
  return fn;
}

inline GLBindFramebufferProc get_gl_bind_framebuffer() {
  static auto fn = reinterpret_cast<GLBindFramebufferProc>(
      SDL_GL_GetProcAddress("glBindFramebuffer"));
  return fn;
}

inline GLBindRenderbufferProc get_gl_bind_renderbuffer() {
  static auto fn = reinterpret_cast<GLBindRenderbufferProc>(
      SDL_GL_GetProcAddress("glBindRenderbuffer"));
  return fn;
}

inline GLGetRenderbufferParameterivProc get_gl_renderbuffer_parameter_iv() {
  static auto fn = reinterpret_cast<GLGetRenderbufferParameterivProc>(
      SDL_GL_GetProcAddress("glGetRenderbufferParameteriv"));
  return fn;
}

inline GrGLenum pick_default_fbo_format() {
  // Prefer sRGB if the default framebuffer advertises it.
#ifdef GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING
  GLint encoding = 0;
  if (auto gl_get_fbo_param = get_framebuffer_attachment_param_iv()) {
    gl_get_fbo_param(GL_FRAMEBUFFER, GL_BACK_LEFT,
                     GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
  }
#ifdef GL_SRGB
  if (encoding == GL_SRGB) {
#ifdef GL_SRGB8_ALPHA8
    return GL_SRGB8_ALPHA8;
#endif
  }
#endif
#endif

#ifdef GL_IMPLEMENTATION_COLOR_READ_FORMAT
  GLint read_format = 0;
  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &read_format);
  switch (read_format) {
  case GL_RGBA:
#ifdef GL_RGBA8
    return GL_RGBA8;
#endif
    break;
#ifdef GL_BGRA
  case GL_BGRA:
#ifdef GL_BGRA8_EXT
    return GL_BGRA8_EXT;
#elif defined(GL_RGBA8)
    return GL_RGBA8;
#endif
    break;
#endif
#ifdef GL_RGB
  case GL_RGB:
#ifdef GL_RGB8
    return GL_RGB8;
#elif defined(GL_RGBA8)
    return GL_RGBA8;
#endif
    break;
#endif
  default:
    break;
  }
#endif

#ifdef GL_RGBA8
  return GL_RGBA8;
#else
  return 0;
#endif
}

inline GrGLenum pick_fbo_format(GrGLuint fbo) {
  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  auto gl_bind_framebuffer = get_gl_bind_framebuffer();
  GrGLenum format = 0;
  if (gl_bind_framebuffer) {
    gl_bind_framebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));

    if (fbo == 0) {
      format = pick_default_fbo_format();
    } else {
#ifdef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
      GLint object_type = 0;
      GLint object_name = 0;
      if (auto gl_get_fbo_param = get_framebuffer_attachment_param_iv()) {
        gl_get_fbo_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
        gl_get_fbo_param(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
      }

#ifdef GL_RENDERBUFFER
      if (object_type == GL_RENDERBUFFER && object_name != 0) {
        GLint prev_rb = 0;
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_rb);
        auto gl_bind_renderbuffer = get_gl_bind_renderbuffer();
        if (gl_bind_renderbuffer) {
          gl_bind_renderbuffer(GL_RENDERBUFFER,
                               static_cast<GLuint>(object_name));
        }
        GLint rb_format = 0;
#ifdef GL_RENDERBUFFER_INTERNAL_FORMAT
        if (auto gl_get_rb_param = get_gl_renderbuffer_parameter_iv()) {
          gl_get_rb_param(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT,
                          &rb_format);
        }
#endif
        if (gl_bind_renderbuffer) {
          gl_bind_renderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_rb));
        }
        format = static_cast<GrGLenum>(rb_format);
      }
#endif
#endif
    }

    gl_bind_framebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
  }

#ifdef GL_RGBA8
  if (format == 0) {
    format = GL_RGBA8;
  }
#endif
  return format;
}

struct Window : bench::BenchmarkWindow {
  SDL_Window *window = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;

  Example *example = nullptr;

  bool initialized = false;
  std::string window_title_;

  Window(Example *example, uint32_t target_width, uint32_t target_height,
         const std::string &window_title = "Benchmark")
      : width(target_width), height(target_height), example(example),
        window_title_(window_title) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
      return;
    }
    initialized = true;
  }

  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  ~Window() override {
    delete example;
    example = nullptr;

    if (window) {
      SDL_DestroyWindow(window);
      window = nullptr;
    }

    if (initialized) {
      SDL_Quit();
    }
  }

  bool ready() override {
    SkCanvas *canvas = sk_canvas();
    if (!canvas || !example) {
      return false;
    }
    if (!example->content(canvas, width, height)) {
      return false;
    }
    return draw();
  }

  bool update(uint32_t frame_index) override {
    SkCanvas *canvas = sk_canvas();
    if (!canvas || !example) {
      return false;
    }
    example->elapsed = frame_index;
    return example->update(canvas, example->elapsed);
  }

  bool draw() override {
    SkCanvas *canvas = sk_canvas();
    if (!canvas || !example) {
      return false;
    }
    return example->draw(canvas);
  }

  uint32_t surface_width() const override { return width; }
  uint32_t surface_height() const override { return height; }

  const char *engine_id() const override { return "skia"; }
  const char *engine_title() const override { return "Skia"; }

protected:
  virtual SkCanvas *sk_canvas() = 0;
};

struct SwWindow final : Window {
  SDL_Surface *surface = nullptr;
  sk_sp<SkSurface> sk_surface;

  SwWindow(Example *example, uint32_t target_width, uint32_t target_height,
           const std::string &window_title = "Benchmark")
      : Window(example, target_width, target_height, window_title) {
    if (!initialized) {
      return;
    }

    std::string title = window_title_ + " Skia (Software)";
    window =
        SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, static_cast<int>(target_width),
                         static_cast<int>(target_height),
                         SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    (void)adjust_window_surface_to_target(window, target_width, target_height);
    resize();
  }

  ~SwWindow() override { sk_surface.reset(); }

  void resize() {
    if (!window) {
      return;
    }

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
      std::cerr << "SDL_GetWindowSurface failed: " << SDL_GetError() << "\n";
      return;
    }

    SkImageInfo image_info;
    if (!skia_image_info_from_sdl_surface(surface, image_info)) {
      return;
    }

    sk_surface = SkSurfaces::WrapPixels(image_info, surface->pixels,
                                        static_cast<size_t>(surface->pitch));
    if (!sk_surface) {
      std::cerr << "Failed to create Skia raster surface\n";
      return;
    }

    width = static_cast<uint32_t>(surface->w);
    height = static_cast<uint32_t>(surface->h);
  }

  void refresh() override {
    if (window) {
      SDL_UpdateWindowSurface(window);
    }
  }

  const char *backend_id() const override { return "cpu"; }
  const char *backend_title() const override { return "CPU"; }

protected:
  SkCanvas *sk_canvas() override {
    return sk_surface ? sk_surface->getCanvas() : nullptr;
  }
};

struct GlWindow final : Window {
  SDL_GLContext context = nullptr;

  sk_sp<const GrGLInterface> gl_interface;
  sk_sp<GrDirectContext> gr_context;
  sk_sp<SkSurface> surface;

  bool gpu_sync = false;

  GlWindow(Example *example, uint32_t target_width, uint32_t target_height,
           bool vsync, bool gpu_sync,
           const std::string &window_title = "Benchmark")
      : Window(example, target_width, target_height, window_title),
        gpu_sync(gpu_sync) {
    if (!initialized) {
      return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    std::string title = window_title_ + " Skia (OpenGL)";
    window = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, static_cast<int>(target_width),
        static_cast<int>(target_height),
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
      std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
      return;
    }

    SDL_GL_SetSwapInterval(vsync ? 1 : 0);

    auto dims = bench::adjust_window_for_hidpi(window, target_width,
                                                   target_height, true);
    width = static_cast<uint32_t>(dims.drawable_w);
    height = static_cast<uint32_t>(dims.drawable_h);

    gl_interface = GrGLMakeNativeInterface();
    if (!gl_interface || !gl_interface->validate()) {
      std::cerr
          << "Failed to create/validate GrGLInterface (is an OpenGL context "
             "current?)\n";
      return;
    }

    gr_context = GrDirectContexts::MakeGL(gl_interface);
    if (!gr_context) {
      std::cerr << "Failed to create GrDirectContext (Ganesh GL)\n";
      return;
    }

    const GrGLuint fbo = current_draw_fbo();
    GrGLFramebufferInfo fb_info;
    fb_info.fFBOID = fbo;
    fb_info.fFormat = pick_fbo_format(fbo);

    GLint stencil_bits = 0;
    glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);

    GLint samples = 0;
#ifdef GL_SAMPLES
    glGetIntegerv(GL_SAMPLES, &samples);
#endif

    const int sample_cnt = samples <= 1 ? 0 : samples;

    std::cout << "GL framebuffer info: fbo=" << fb_info.fFBOID << " format=0x"
              << std::hex << fb_info.fFormat << std::dec
              << " samples=" << sample_cnt << " stencil=" << stencil_bits
              << "\n";
    if (stencil_bits == 0) {
      std::cout
          << "Warning: default framebuffer has 0 stencil bits; some Skia "
             "GPU operations (e.g. complex clips/MSAA paths) may not work "
             "correctly.\n";
    }

    GrBackendRenderTarget backend_rt = GrBackendRenderTargets::MakeGL(
        static_cast<int>(width), static_cast<int>(height), sample_cnt,
        stencil_bits, fb_info);

    surface = SkSurfaces::WrapBackendRenderTarget(
        gr_context.get(), backend_rt, kBottomLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType, /*colorSpace=*/nullptr,
        /*surfaceProps=*/nullptr);

    if (!surface) {
      std::cerr << "Failed to create GPU SkSurface from window framebuffer.\n"
                   "Common causes: incorrect GrGLFramebufferInfo.fFormat or "
                   "missing stencil.\n";
      return;
    }
  }

  ~GlWindow() override {
    surface.reset();
    gr_context.reset();
    gl_interface.reset();

    if (context) {
      SDL_GL_DeleteContext(context);
      context = nullptr;
    }
  }

  bool draw() override {
    if (!surface) {
      return false;
    }

    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
#ifdef GL_BACK
    glDrawBuffer(GL_BACK);
#endif

    if (!Window::draw()) {
      return false;
    }

    skgpu::ganesh::FlushAndSubmit(surface.get());

    if (gpu_sync) {
      glFinish();
    }

    return true;
  }

  void refresh() override {
    if (window) {
      SDL_GL_SwapWindow(window);
    }
  }

  const char *backend_id() const override { return "gl"; }
  const char *backend_title() const override { return "OpenGL"; }

protected:
  SkCanvas *sk_canvas() override {
    return surface ? surface->getCanvas() : nullptr;
  }
};

} // namespace bench::skiaexam
