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

#define GL_SILENCE_DEPRECATION
#ifndef __APPLE__
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#endif
#include <SDL2/SDL_opengl.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#ifndef VGBENCH_THORVG_VERSION
#define VGBENCH_THORVG_VERSION "unknown"
#endif

#ifndef VGBENCH_THORVG_REVISION
#define VGBENCH_THORVG_REVISION "unknown"
#endif

namespace bench::tvgexam {

inline constexpr uint32_t kThreadCount = 4;

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
  virtual const char *asset_hash() const { return ""; }

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
         const std::string &window_title = "Benchmark")
      : width(target_width), height(target_height), example(example),
        window_title_(window_title) {
    if (!verify(tvg::Initializer::init(kThreadCount),
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
    if (!update(0)) {
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
  const char *engine_version() const override { return VGBENCH_THORVG_VERSION; }
  const char *engine_revision() const override {
    return VGBENCH_THORVG_REVISION;
  }
  const char *scene_model() const override { return "retained"; }
  const char *asset_hash() const override {
    return example ? example->asset_hash() : "";
  }
};

// Matches thorvg.example's SwWindow presentation path: `SDL_GetWindowSurface()`
// + `SDL_UpdateWindowSurface()` (useful for 1:1 comparison with official example).
// The HiDPI adjustment is kept so the actual pixel workload matches `target_width/height`.
struct SwWindow final : Window {
  SDL_Surface *surface = nullptr;

  SwWindow(Example *example, uint32_t target_width, uint32_t target_height,
           bool vsync,
           const std::string &window_title = "Benchmark",
           tvg::EngineOption engine_option = tvg::EngineOption::None)  // disable partial rendering for SwEngine
      : Window(example, target_width, target_height, window_title) {
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
  const char *graphics_api() const override { return "CPU"; }
  const char *present_mode() const override { return "software"; }
  const char *pixel_format() const override { return "RGBA8"; }

  bool capture(const std::string &path) override {
    return bench::write_ppm_sdl_surface(path, surface);
  }
};

struct GlWindow final : Window {
  SDL_GLContext context = nullptr;
  bool gpu_sync = false;
  bool actual_vsync = false;
  bool verified_vsync = false;
  std::string present_mode_ = "unknown";
  std::string gpu_device_ = "unknown";
  std::string gpu_vendor_ = "unknown";
  std::string gpu_driver_ = "unknown";

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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

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
    if (SDL_GL_MakeCurrent(window, context) < 0) {
      std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << "\n";
      return;
    }

    const int swap_result = SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    const int swap_interval = SDL_GL_GetSwapInterval();
    actual_vsync = swap_interval != 0;
    verified_vsync = swap_result == 0 && actual_vsync == vsync;
    if (swap_interval == 0) {
      present_mode_ = "immediate";
    } else if (swap_interval == -1) {
      present_mode_ = "adaptive-fifo";
    } else {
      present_mode_ = "fifo";
    }
    if (!verified_vsync) {
      std::cerr << "Unable to verify requested GL swap interval "
                << (vsync ? 1 : 0) << "; actual=" << swap_interval << ": "
                << SDL_GetError() << "\n";
    }
    if (!bench::verify_gl33_core_context("ThorVG")) {
      return;
    }

    auto dims = bench::adjust_window_for_hidpi(window, target_width,
                                                   target_height, true);
    width = static_cast<uint32_t>(dims.drawable_w);
    height = static_cast<uint32_t>(dims.drawable_h);

    // ThorVG's GL loader exports a function-pointer variable named
    // `glGetString`. When ThorVG is linked statically, that data symbol can
    // override the platform GL function in this executable. Calling
    // `glGetString` directly then branches into data and crashes. Resolve the
    // function for this SDL context explicitly to avoid the symbol collision.
    using GlGetStringProc = const GLubyte *(APIENTRYP)(GLenum);
    const auto get_string = reinterpret_cast<GlGetStringProc>(
        SDL_GL_GetProcAddress("glGetString"));
    if (!get_string) {
      std::cerr << "SDL_GL_GetProcAddress(glGetString) failed: "
                << SDL_GetError() << "\n";
      return;
    }
    const auto *renderer = get_string(GL_RENDERER);
    const auto *vendor = get_string(GL_VENDOR);
    const auto *version = get_string(GL_VERSION);
    if (renderer) gpu_device_ = reinterpret_cast<const char *>(renderer);
    if (vendor) gpu_vendor_ = reinterpret_cast<const char *>(vendor);
    if (version) gpu_driver_ = reinterpret_cast<const char *>(version);

    canvas = tvg::GlCanvas::gen();
    if (!canvas) {
      std::cerr << "GlCanvas is not supported. Did you enable the GlEngine?\n";
      return;
    }

    if (!verify(static_cast<tvg::GlCanvas *>(canvas)->target(
            nullptr, nullptr, context, static_cast<int32_t>(0), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
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
  const char *graphics_api() const override { return "OpenGL"; }
  const char *gpu_device() const override { return gpu_device_.c_str(); }
  const char *gpu_vendor() const override { return gpu_vendor_.c_str(); }
  const char *gpu_driver() const override { return gpu_driver_.c_str(); }
  const char *gpu_completion() const override {
    return gpu_sync ? "glFinish" : "none";
  }
  const char *present_mode() const override { return present_mode_.c_str(); }
  const char *pixel_format() const override { return "RGBA8"; }
  bool vsync_actual() const override { return actual_vsync; }
  bool vsync_verified() const override { return verified_vsync; }

  bool finish_gpu() override {
    if (gpu_sync) {
      glFinish();
    }
    return true;
  }

  bool capture(const std::string &path) override {
    if (!context || width == 0 || height == 0) {
      return false;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
#ifdef GL_BACK
    glReadBuffer(GL_BACK);
#endif
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    return bench::write_ppm_rgba(path, pixels.data(), width, height,
                                 static_cast<size_t>(width) * 4u,
                                 /*flip_y=*/true);
  }
};


struct WgpuContext {
  WGPUInstance instance = nullptr;
  WGPUAdapter adapter = nullptr;
  WGPUDevice device = nullptr;
  WGPUQueue queue = nullptr;
  WGPUSurface surface = nullptr;
  WGPUSurfaceConfiguration config = {};
  WGPUPresentMode selected_present_mode = WGPUPresentMode_Undefined;
  bool present_mode_verified = false;
  std::string adapter_device = "unknown";
  std::string adapter_vendor = "unknown";
  std::string adapter_description = "unknown";
  std::string graphics_api = "WebGPU unknown";

#ifdef __APPLE__
  SDL_MetalView metalView = nullptr;
#endif

  static std::string string_view(WGPUStringView view) {
    if (!view.data) return "unknown";
    if (view.length == WGPU_STRLEN) return std::string(view.data);
    return std::string(view.data, view.length);
  }

  static const char *backend_name(WGPUBackendType backend) {
    switch (backend) {
    case WGPUBackendType_D3D11: return "D3D11";
    case WGPUBackendType_D3D12: return "D3D12";
    case WGPUBackendType_Metal: return "Metal";
    case WGPUBackendType_Vulkan: return "Vulkan";
    case WGPUBackendType_OpenGL: return "OpenGL";
    case WGPUBackendType_OpenGLES: return "OpenGL ES";
    case WGPUBackendType_WebGPU: return "WebGPU";
    default: return "WebGPU";
    }
  }

  static bool supports_present_mode(const WGPUSurfaceCapabilities &caps,
                                    WGPUPresentMode mode) {
    for (size_t i = 0; i < caps.presentModeCount; ++i) {
      if (caps.presentModes[i] == mode) return true;
    }
    return false;
  }

  bool init(SDL_Window *window, uint32_t width, uint32_t height,
            bool requested_vsync) {
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

    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;

    struct AdapterUserData {
      WGPUAdapter adapter = nullptr;
      bool done = false;
    } adapterData;

    WGPURequestAdapterCallbackInfo adapterCbInfo = {};
    adapterCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCbInfo.callback = [](WGPURequestAdapterStatus status,
                                WGPUAdapter adapter, WGPUStringView message,
                                void *userdata, void *) {
      auto *data = static_cast<AdapterUserData *>(userdata);
      if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
      } else {
        std::cerr << "Adapter request failed: "
                  << WgpuContext::string_view(message) << "\n";
      }
      data->done = true;
    };
    adapterCbInfo.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCbInfo);

    while (!adapterData.done) {
      wgpuInstanceProcessEvents(instance);
    }

    if (!adapterData.adapter) {
      std::cerr << "Failed to get WGPUAdapter\n";
      return false;
    }
    adapter = adapterData.adapter;

    WGPUAdapterInfo adapter_info = {};
    if (wgpuAdapterGetInfo(adapter, &adapter_info) == WGPUStatus_Success) {
      adapter_device = string_view(adapter_info.device);
      adapter_vendor = string_view(adapter_info.vendor);
      adapter_description = string_view(adapter_info.description);
      graphics_api = std::string("WebGPU ") + backend_name(adapter_info.backendType);
      wgpuAdapterInfoFreeMembers(adapter_info);
    }

    struct DeviceUserData {
      WGPUDevice device = nullptr;
      bool done = false;
    } deviceData;

    WGPURequestDeviceCallbackInfo deviceCbInfo = {};
    deviceCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                               WGPUStringView message, void *userdata, void *) {
      auto *data = static_cast<DeviceUserData *>(userdata);
      if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
      } else {
        std::cerr << "Device request failed: "
                  << WgpuContext::string_view(message) << "\n";
      }
      data->done = true;
    };
    deviceCbInfo.userdata1 = &deviceData;

    WGPUDeviceDescriptor deviceDesc = {};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCbInfo);

    while (!deviceData.done) {
      wgpuInstanceProcessEvents(instance);
    }

    if (!deviceData.device) {
      std::cerr << "Failed to get WGPUDevice\n";
      return false;
    }
    device = deviceData.device;
    queue = wgpuDeviceGetQueue(device);
    if (!queue) {
      std::cerr << "Failed to get WebGPU queue\n";
      return false;
    }

    WGPUSurfaceCapabilities capabilities = {};
    if (wgpuSurfaceGetCapabilities(surface, adapter, &capabilities) !=
        WGPUStatus_Success) {
      std::cerr << "Failed to query WebGPU surface capabilities\n";
      return false;
    }

    bool supports_bgra8 = false;
    for (size_t i = 0; i < capabilities.formatCount; ++i) {
      supports_bgra8 = supports_bgra8 ||
                       capabilities.formats[i] == WGPUTextureFormat_BGRA8Unorm;
    }
    if (!supports_bgra8) {
      std::cerr << "WebGPU surface does not support required BGRA8 format\n";
      wgpuSurfaceCapabilitiesFreeMembers(capabilities);
      return false;
    }

    if (requested_vsync) {
      selected_present_mode = WGPUPresentMode_Fifo;
      present_mode_verified =
          supports_present_mode(capabilities, selected_present_mode);
    } else if (supports_present_mode(capabilities,
                                     WGPUPresentMode_Immediate)) {
      selected_present_mode = WGPUPresentMode_Immediate;
      present_mode_verified = true;
    } else if (supports_present_mode(capabilities,
                                     WGPUPresentMode_Mailbox)) {
      selected_present_mode = WGPUPresentMode_Mailbox;
      present_mode_verified = true;
    } else {
      selected_present_mode = WGPUPresentMode_Fifo;
      present_mode_verified = false;
    }
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    config.device = device;
    config.format = WGPUTextureFormat_BGRA8Unorm;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = selected_present_mode;
    wgpuSurfaceConfigure(surface, &config);

    return true;
  }

  void destroy() {
    if (queue) {
      wgpuQueueRelease(queue);
      queue = nullptr;
    }
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

  void configure_surface() {
    if (surface && device) {
      wgpuSurfaceConfigure(surface, &config);
    }
  }

  bool finish() {
    if (!queue || !device || !instance) {
      return false;
    }

    struct QueueDone {
      bool complete = false;
      bool success = false;
    } done;

    WGPUQueueWorkDoneCallbackInfo callback_info = {};
    callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
    callback_info.callback = [](WGPUQueueWorkDoneStatus status,
                                void *userdata, void *) {
      auto *state = static_cast<QueueDone *>(userdata);
      state->success = status == WGPUQueueWorkDoneStatus_Success;
      state->complete = true;
    };
    callback_info.userdata1 = &done;
    wgpuQueueOnSubmittedWorkDone(queue, callback_info);

    while (!done.complete) {
      wgpuDevicePoll(device, true, nullptr);
      wgpuInstanceProcessEvents(instance);
    }
    return done.success;
  }

  bool capture_texture(WGPUTexture texture, const std::string &path,
                       uint32_t width, uint32_t height) {
    if (!texture || !device || !queue || !instance || width == 0 ||
        height == 0) {
      return false;
    }

    constexpr uint32_t kCopyRowAlignment = 256;
    const uint32_t unpadded_bytes_per_row = width * 4u;
    const uint32_t padded_bytes_per_row =
        (unpadded_bytes_per_row + kCopyRowAlignment - 1u) &
        ~(kCopyRowAlignment - 1u);
    const uint64_t buffer_size =
        static_cast<uint64_t>(padded_bytes_per_row) * height;

    WGPUBufferDescriptor buffer_desc = {};
    buffer_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    buffer_desc.size = buffer_size;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
    if (!buffer) return false;

    WGPUCommandEncoderDescriptor encoder_desc = {};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(device, &encoder_desc);
    if (!encoder) {
      wgpuBufferRelease(buffer);
      return false;
    }

    WGPUTexelCopyTextureInfo source = {};
    source.texture = texture;
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = buffer;
    destination.layout.bytesPerRow = padded_bytes_per_row;
    destination.layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination,
                                          &extent);
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (!command) {
      wgpuBufferRelease(buffer);
      return false;
    }
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);

    struct MapDone {
      bool complete = false;
      bool success = false;
    } done;

    WGPUBufferMapCallbackInfo callback_info = {};
    callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
    callback_info.callback = [](WGPUMapAsyncStatus status, WGPUStringView message,
                                void *userdata, void *) {
      auto *state = static_cast<MapDone *>(userdata);
      if (status != WGPUMapAsyncStatus_Success) {
        std::cerr << "WebGPU capture map failed: "
                  << WgpuContext::string_view(message) << "\n";
      }
      state->success = status == WGPUMapAsyncStatus_Success;
      state->complete = true;
    };
    callback_info.userdata1 = &done;
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0,
                       static_cast<size_t>(buffer_size), callback_info);

    while (!done.complete) {
      wgpuDevicePoll(device, true, nullptr);
      wgpuInstanceProcessEvents(instance);
    }
    if (!done.success) {
      wgpuBufferRelease(buffer);
      return false;
    }

    const auto *mapped = static_cast<const uint8_t *>(
        wgpuBufferGetConstMappedRange(buffer, 0,
                                      static_cast<size_t>(buffer_size)));
    if (!mapped) {
      wgpuBufferUnmap(buffer);
      wgpuBufferRelease(buffer);
      return false;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
      const uint8_t *source_row =
          mapped + static_cast<size_t>(y) * padded_bytes_per_row;
      uint8_t *destination_row =
          rgba.data() + static_cast<size_t>(y) * unpadded_bytes_per_row;
      for (uint32_t x = 0; x < width; ++x) {
        const size_t offset = static_cast<size_t>(x) * 4u;
        destination_row[offset] = source_row[offset + 2u];
        destination_row[offset + 1u] = source_row[offset + 1u];
        destination_row[offset + 2u] = source_row[offset];
        destination_row[offset + 3u] = source_row[offset + 3u];
      }
    }

    wgpuBufferUnmap(buffer);
    wgpuBufferRelease(buffer);
    return bench::write_ppm_rgba(path, rgba.data(), width, height,
                                  static_cast<size_t>(width) * 4u);
  }

  const char *present_mode_name() const {
    switch (selected_present_mode) {
    case WGPUPresentMode_Immediate: return "immediate";
    case WGPUPresentMode_Mailbox: return "mailbox";
    case WGPUPresentMode_FifoRelaxed: return "fifo-relaxed";
    case WGPUPresentMode_Fifo: return "fifo";
    default: return "unknown";
    }
  }
};

struct WgWindow final : Window {
  WgpuContext wgpu;
  bool gpu_sync = false;

  WgWindow(Example *example, uint32_t target_width, uint32_t target_height,
           bool vsync, bool gpu_sync,
           const std::string &window_title = "Benchmark")
      : Window(example, target_width, target_height, window_title),
        gpu_sync(gpu_sync) {
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

    if (!wgpu.init(window, width, height, vsync)) {
      std::cerr << "Failed to initialize WebGPU context\n";
      return;
    }

    canvas = tvg::WgCanvas::gen();
    if (!canvas) {
      std::cerr << "WgCanvas is not supported. Did you enable the WgEngine?\n";
      return;
    }

    if (!verify(static_cast<tvg::WgCanvas *>(canvas)->target(
            wgpu.device, wgpu.instance, wgpu.surface,
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            tvg::ColorSpace::ABGR8888S, 0),
                "Failed to set WgCanvas target")) {
      return;
    }

    // ThorVG configures the supplied surface internally. Reapply the harness
    // selection so Immediate/Mailbox policy remains authoritative.
    wgpu.configure_surface();
  }

  ~WgWindow() override {
    delete canvas;
    canvas = nullptr;
    wgpu.destroy();
  }

  void refresh() override { wgpu.present(); }

  const char *backend_id() const override { return "webgpu"; }
  const char *backend_title() const override { return "WebGPU"; }
  const char *graphics_api() const override {
    return wgpu.graphics_api.c_str();
  }
  const char *gpu_device() const override {
    return wgpu.adapter_device.c_str();
  }
  const char *gpu_vendor() const override {
    return wgpu.adapter_vendor.c_str();
  }
  const char *gpu_driver() const override {
    return wgpu.adapter_description.c_str();
  }
  const char *gpu_completion() const override {
    return gpu_sync ? "queue.onSubmittedWorkDone" : "none";
  }
  const char *present_mode() const override {
    return wgpu.present_mode_name();
  }
  const char *pixel_format() const override { return "BGRA8"; }
  bool vsync_actual() const override {
    return wgpu.selected_present_mode != WGPUPresentMode_Immediate;
  }
  bool vsync_verified() const override {
    return wgpu.present_mode_verified;
  }

  bool finish_gpu() override { return !gpu_sync || wgpu.finish(); }

  bool capture(const std::string &path) override {
    if (!canvas || !wgpu.device || !wgpu.instance || !wgpu.surface) {
      return false;
    }

    WGPUTextureDescriptor texture_desc = {};
    texture_desc.usage =
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = {width, height, 1};
    texture_desc.format = WGPUTextureFormat_BGRA8Unorm;
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    WGPUTexture texture = wgpuDeviceCreateTexture(wgpu.device, &texture_desc);
    if (!texture) return false;

    auto *wg_canvas = static_cast<tvg::WgCanvas *>(canvas);
    bool captured = false;
    if (verify(wg_canvas->target(wgpu.device, wgpu.instance, texture, width,
                                 height, tvg::ColorSpace::ABGR8888S, 1),
               "Failed to set WebGPU capture target") &&
        update(0) && draw()) {
      captured = wgpu.capture_texture(texture, path, width, height);
    }

    const bool restored = verify(
        wg_canvas->target(wgpu.device, wgpu.instance, wgpu.surface, width,
                          height, tvg::ColorSpace::ABGR8888S, 0),
        "Failed to restore WebGPU surface target");
    if (restored) wgpu.configure_surface();
    wgpuTextureRelease(texture);
    return captured && restored;
  }
};

} // namespace bench::tvgexam
