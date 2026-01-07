#pragma once

// A small, Example.h-inspired SDL + ThorVG window framework used by rectbench.
// Goal: make setup/conditions easy to compare with thorvg.example.

#include "benchmark_window.hpp"
#include "sdl_utils.hpp"

#include <thorvg.h>

#include <SDL2/SDL.h>
#ifdef __APPLE__
#include <SDL2/SDL_metal.h>
#endif

#include <webgpu/webgpu.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace bench::tvgexam {

inline uint32_t max_thread_count() {
  const unsigned int count = std::thread::hardware_concurrency();
  return count > 0 ? static_cast<uint32_t>(count) : 1u;
}

inline bool verify(tvg::Result result, const std::string &fail_msg = {}) {
  switch (result) {
  case tvg::Result::Success:
    return true;
  case tvg::Result::FailedAllocation:
    std::cerr << "ThorVG FailedAllocation: " << fail_msg << "\n";
    return false;
  case tvg::Result::InsufficientCondition:
    std::cerr << "ThorVG InsufficientCondition: " << fail_msg << "\n";
    return false;
  case tvg::Result::InvalidArguments:
    std::cerr << "ThorVG InvalidArguments: " << fail_msg << "\n";
    return false;
  case tvg::Result::MemoryCorruption:
    std::cerr << "ThorVG MemoryCorruption: " << fail_msg << "\n";
    return false;
  case tvg::Result::NonSupport:
    std::cerr << "ThorVG NonSupport: " << fail_msg << "\n";
    return false;
  case tvg::Result::Unknown:
    std::cerr << "ThorVG Unknown: " << fail_msg << "\n";
    return false;
  }
  return false;
}

struct Example {
  uint32_t elapsed = 0;

  virtual bool content(tvg::Canvas *canvas, uint32_t w, uint32_t h) = 0;
  virtual bool update(tvg::Canvas *canvas, uint32_t elapsed) {
    (void)canvas;
    (void)elapsed;
    return false;
  }

  virtual ~Example() = default;
};

struct Window : bench::BenchmarkWindow {
  SDL_Window *window = nullptr;
  tvg::Canvas *canvas = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;

  Example *example = nullptr;

  bool initialized = false;
  bool clearBuffer = true;
  std::string window_title_;

  Window(Example *example, uint32_t target_width, uint32_t target_height,
         uint32_t threads_cnt, const std::string &window_title = "Benchmark")
      : width(target_width), height(target_height), example(example),
        window_title_(window_title) {
    if (!verify(tvg::Initializer::init(threads_cnt),
                "Failed to init ThorVG engine")) {
      return;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
      tvg::Initializer::term();
      return;
    }
    initialized = true;
  }

  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  virtual ~Window() {
    delete example;
    example = nullptr;

    delete canvas;
    canvas = nullptr;

    if (window) {
      SDL_DestroyWindow(window);
      window = nullptr;
    }

    if (initialized) {
      SDL_Quit();
      tvg::Initializer::term();
    }
  }

  bool pump_events(bool &running) override {
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

  bool draw() override {
    if (!canvas) {
      std::cerr << "Window::draw called without a canvas\n";
      return false;
    }
    if (!verify(canvas->draw(clearBuffer))) {
      return false;
    }
    return verify(canvas->sync());
  }

  bool ready() override {
    if (!canvas || !example) {
      return false;
    }
    if (!example->content(canvas, width, height)) {
      return false;
    }
    if (!verify(canvas->draw())) {
      return false;
    }
    return verify(canvas->sync());
  }

  virtual void resize() {}
  void refresh() override {}

  bool update(uint32_t frame_index) override {
    if (!canvas || !example) {
      return false;
    }
    example->elapsed = frame_index;
    return example->update(canvas, example->elapsed);
  }

  uint32_t surface_width() const override { return width; }
  uint32_t surface_height() const override { return height; }

  const char *engine_id() const override { return "thorvg"; }
  const char *engine_title() const override { return "ThorVG"; }
};

// Matches thorvg.example's SwWindow presentation path: `SDL_GetWindowSurface()`
// + `SDL_UpdateWindowSurface()` (useful for 1:1 comparison with official example).
// The HiDPI adjustment is kept so the actual pixel workload matches `target_width/height`.
struct SwWindow final : Window {
  SDL_Surface *surface = nullptr;

  SwWindow(Example *example, uint32_t target_width, uint32_t target_height,
           uint32_t threads_cnt, bool vsync,
           const std::string &window_title = "Benchmark",
           tvg::EngineOption engine_option = tvg::EngineOption::Default)
      : Window(example, target_width, target_height, threads_cnt, window_title) {
    (void)vsync;
    if (!initialized) {
      return;
    }

    std::string title = window_title_ + " ThorVG (Software)";
    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              static_cast<int>(target_width),
                              static_cast<int>(target_height),
                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    canvas = tvg::SwCanvas::gen(engine_option);
    if (!canvas) {
      std::cerr << "SwCanvas is not supported. Did you enable the SwEngine?\n";
      return;
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
        break;
      }

      const float scale_x =
          static_cast<float>(current_surface->w) / static_cast<float>(window_w);
      const float scale_y =
          static_cast<float>(current_surface->h) / static_cast<float>(window_h);

      if (scale_x <= 0.0f || scale_y <= 0.0f) {
        break;
      }

      if (static_cast<uint32_t>(current_surface->w) == target_width &&
          static_cast<uint32_t>(current_surface->h) == target_height) {
        break;
      }

      const int new_window_w =
          static_cast<int>(static_cast<float>(target_width) / scale_x + 0.5f);
      const int new_window_h =
          static_cast<int>(static_cast<float>(target_height) / scale_y + 0.5f);

      if (new_window_w <= 0 || new_window_h <= 0) {
        break;
      }
      if (new_window_w == window_w && new_window_h == window_h) {
        break;
      }

      SDL_SetWindowSize(window, new_window_w, new_window_h);
    }

    resize();
  }

  void resize() override {
    if (!window || !canvas) {
      return;
    }

    surface = SDL_GetWindowSurface(window);
    if (!surface) {
      std::cerr << "SDL_GetWindowSurface failed: " << SDL_GetError() << "\n";
      return;
    }
    if (surface->format->BytesPerPixel != 4 || (surface->pitch % 4) != 0) {
      std::cerr << "Unsupported window surface format for SwCanvas target.\n";
      return;
    }

    width = static_cast<uint32_t>(surface->w);
    height = static_cast<uint32_t>(surface->h);

    (void)verify(static_cast<tvg::SwCanvas *>(canvas)->target(
                     reinterpret_cast<uint32_t *>(surface->pixels),
                     static_cast<uint32_t>(surface->pitch) / 4u,
                     static_cast<uint32_t>(surface->w),
                     static_cast<uint32_t>(surface->h),
                     tvg::ColorSpace::ARGB8888),
                 "Failed to set SwCanvas target");
  }

  void refresh() override {
    if (window) {
      SDL_UpdateWindowSurface(window);
    }
  }

  const char *backend_id() const override { return "cpu"; }
  const char *backend_title() const override { return "CPU"; }
};

struct GlWindow final : Window {
  SDL_GLContext context = nullptr;

  GlWindow(Example *example, uint32_t target_width, uint32_t target_height,
           uint32_t threads_cnt, bool vsync,
           const std::string &window_title = "Benchmark")
      : Window(example, target_width, target_height, threads_cnt, window_title) {
    if (!initialized) {
      return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    std::string title = window_title_ + " ThorVG (OpenGL)";
    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              static_cast<int>(target_width),
                              static_cast<int>(target_height),
                              SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL |
                                  SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
      std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
      return;
    }

    if (SDL_GL_SetSwapInterval(vsync ? 1 : 0) < 0) {
      std::cerr << "Warning: Unable to set VSync: " << SDL_GetError() << "\n";
    }

    auto dims = bench::adjust_window_for_hidpi(window, target_width,
                                                   target_height, true);
    width = static_cast<uint32_t>(dims.drawable_w);
    height = static_cast<uint32_t>(dims.drawable_h);

    canvas = tvg::GlCanvas::gen();
    if (!canvas) {
      std::cerr << "GlCanvas is not supported. Did you enable the GlEngine?\n";
      return;
    }

    if (!verify(static_cast<tvg::GlCanvas *>(canvas)->target(
            context, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            tvg::ColorSpace::ABGR8888S),
                "Failed to set GlCanvas target")) {
      return;
    }
  }

  ~GlWindow() override {
    delete canvas;
    canvas = nullptr;

    if (context) {
      SDL_GL_DeleteContext(context);
      context = nullptr;
    }
  }

  void refresh() override {
    if (window) {
      SDL_GL_SwapWindow(window);
    }
  }

  const char *backend_id() const override { return "gl"; }
  const char *backend_title() const override { return "OpenGL"; }
};


struct WgpuContext {
  WGPUInstance instance = nullptr;
  WGPUAdapter adapter = nullptr;
  WGPUDevice device = nullptr;
  WGPUSurface surface = nullptr;
  WGPUSurfaceConfiguration config = {};

#ifdef __APPLE__
  SDL_MetalView metalView = nullptr;
#endif

  bool init(SDL_Window *window, uint32_t width, uint32_t height,
            bool use_external_device) {
    WGPUInstanceDescriptor instanceDesc = {};
    instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
      std::cerr << "Failed to create WGPUInstance\n";
      return false;
    }

#ifdef __APPLE__
    metalView = SDL_Metal_CreateView(window);
    if (!metalView) {
      std::cerr << "Failed to create SDL Metal view: " << SDL_GetError() << "\n";
      return false;
    }

    void *metalLayer = SDL_Metal_GetLayer(metalView);
    if (!metalLayer) {
      std::cerr << "Failed to get CAMetalLayer\n";
      return false;
    }

    WGPUSurfaceSourceMetalLayer metalDesc = {};
    metalDesc.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalDesc.layer = metalLayer;

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (WGPUChainedStruct *)&metalDesc;

    surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);
#else
    std::cerr << "WebGPU surface creation not implemented for this platform\n";
    return false;
#endif

    if (!surface) {
      std::cerr << "Failed to create WGPUSurface\n";
      return false;
    }

    // For external device mode, ThorVG owns adapter/device/surface configuration.
    if (use_external_device) {
      return true;
    }

    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;

    struct AdapterUserData {
      WGPUAdapter adapter = nullptr;
      bool done = false;
      bool failed = false;
    } adapterData;

    WGPURequestAdapterCallbackInfo adapterCbInfo = {};
    adapterCbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCbInfo.callback = [](WGPURequestAdapterStatus status,
                                WGPUAdapter adapter, WGPUStringView message,
                                void *userdata, void *) {
      auto *data = static_cast<AdapterUserData *>(userdata);
      if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
      } else {
        std::cerr << "Adapter request failed: "
                  << (message.length ? message.data : "unknown") << "\n";
        data->failed = true;
      }
      data->done = true;
    };
    adapterCbInfo.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCbInfo);

    while (!adapterData.done && !adapterData.failed) {
      wgpuInstanceProcessEvents(instance);
    }

    if (adapterData.failed || !adapterData.adapter) {
      std::cerr << "Failed to get WGPUAdapter\n";
      return false;
    }
    adapter = adapterData.adapter;

    struct DeviceUserData {
      WGPUDevice device = nullptr;
      bool done = false;
      bool failed = false;
    } deviceData;

    WGPURequestDeviceCallbackInfo deviceCbInfo = {};
    deviceCbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                               WGPUStringView message, void *userdata, void *) {
      auto *data = static_cast<DeviceUserData *>(userdata);
      if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
      } else {
        std::cerr << "Device request failed: "
                  << (message.length ? message.data : "unknown") << "\n";
        data->failed = true;
      }
      data->done = true;
    };
    deviceCbInfo.userdata1 = &deviceData;

    WGPUDeviceDescriptor deviceDesc = {};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCbInfo);

    while (!deviceData.done && !deviceData.failed) {
      wgpuInstanceProcessEvents(instance);
    }

    if (deviceData.failed || !deviceData.device) {
      std::cerr << "Failed to get WGPUDevice\n";
      return false;
    }
    device = deviceData.device;

    config.device = device;
    config.format = WGPUTextureFormat_BGRA8Unorm;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &config);

    return true;
  }

  void destroy() {
    if (device) {
      wgpuDeviceRelease(device);
      device = nullptr;
    }
    if (adapter) {
      wgpuAdapterRelease(adapter);
      adapter = nullptr;
    }
    if (surface) {
      wgpuSurfaceRelease(surface);
      surface = nullptr;
    }
    if (instance) {
      wgpuInstanceRelease(instance);
      instance = nullptr;
    }

#ifdef __APPLE__
    if (metalView) {
      SDL_Metal_DestroyView(metalView);
      metalView = nullptr;
    }
#endif
  }

  void present() {
    if (surface) {
      wgpuSurfacePresent(surface);
    }
  }
};

struct WgWindow final : Window {
  WgpuContext wgpu;
  bool use_external_device = false;

  WgWindow(Example *example, uint32_t target_width, uint32_t target_height,
           uint32_t threads_cnt, bool use_external_device,
           const std::string &window_title = "Benchmark")
      : Window(example, target_width, target_height, threads_cnt, window_title),
        use_external_device(use_external_device) {
    if (!initialized) {
      return;
    }

    Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __APPLE__
    window_flags |= SDL_WINDOW_METAL;
#endif

    std::string title = window_title_ + " ThorVG (WebGPU)";
    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              static_cast<int>(target_width),
                              static_cast<int>(target_height), window_flags);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    int drawable_w = static_cast<int>(target_width);
    int drawable_h = static_cast<int>(target_height);

#ifdef __APPLE__
    SDL_MetalView metal_view = SDL_Metal_CreateView(window);
    auto dims = bench::adjust_window_for_hidpi(window, target_width,
                                                   target_height, false,
                                                   nullptr);
    drawable_w = dims.drawable_w;
    drawable_h = dims.drawable_h;
    SDL_Metal_DestroyView(metal_view);
#endif

    width = static_cast<uint32_t>(drawable_w);
    height = static_cast<uint32_t>(drawable_h);

    if (!wgpu.init(window, width, height, use_external_device)) {
      std::cerr << "Failed to initialize WebGPU context\n";
      return;
    }

    canvas = tvg::WgCanvas::gen();
    if (!canvas) {
      std::cerr << "WgCanvas is not supported. Did you enable the WgEngine?\n";
      return;
    }

    if (!verify(static_cast<tvg::WgCanvas *>(canvas)->target(
            use_external_device ? nullptr : wgpu.device, wgpu.instance,
            wgpu.surface, static_cast<int>(width), static_cast<int>(height),
            tvg::ColorSpace::ABGR8888S, 0),
                "Failed to set WgCanvas target")) {
      return;
    }
  }

  ~WgWindow() override {
    delete canvas;
    canvas = nullptr;
    wgpu.destroy();
  }

  void refresh() override { wgpu.present(); }

  const char *backend_id() const override { return "webgpu"; }
  const char *backend_title() const override { return "WebGPU"; }
};

} // namespace bench::tvgexam
