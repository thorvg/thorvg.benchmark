#include "nanovg_bench.hpp"

#include "benchmark_runner.hpp"
#include "circle_generator.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"
#include "sha256.hpp"
#include "sdl_utils.hpp"

#include <SDL2/SDL.h>

#define GL_SILENCE_DEPRECATION
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <SDL2/SDL_opengl.h>
#endif

#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace bench::nanovgbench {
namespace {

#ifndef VGBENCH_NANOVG_VERSION
#define VGBENCH_NANOVG_VERSION "unknown"
#endif

#ifndef VGBENCH_NANOVG_REVISION
#define VGBENCH_NANOVG_REVISION "unknown"
#endif

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

std::string find_image_path(const std::string &extension) {
  const std::string filename = "test." + extension;
  const std::string candidates[] = {
      "../resource/image/" + filename,
      "resource/image/" + filename,
      "../../resource/image/" + filename,
  };

  for (const auto &candidate : candidates) {
    std::ifstream file(candidate, std::ios::binary);
    if (file.good()) {
      return candidate;
    }
  }
  return {};
}

bool set_gl_attribute(SDL_GLattr attribute, int value, const char *name) {
  if (SDL_GL_SetAttribute(attribute, value) == 0) {
    return true;
  }
  std::cerr << "SDL_GL_SetAttribute(" << name << ") failed: " << SDL_GetError()
            << "\n";
  return false;
}

NVGcolor complement(NVGcolor color) {
  return nvgRGBAf(1.0f - color.r, 1.0f - color.g, 1.0f - color.b, color.a);
}

NVGcolor rect_color(const bench::RectData &rect) {
  return nvgRGBA(rect.r, rect.g, rect.b, rect.a);
}

NVGcolor circle_color(const bench::CircleData &circle) {
  return nvgRGBA(circle.r, circle.g, circle.b, circle.a);
}

class NanoVgWindow final : public bench::BenchmarkWindow {
public:
  NanoVgWindow(bench::CliOptions opts, Workload workload)
      : opts_(std::move(opts)), workload_(workload), width_(opts_.width),
        height_(opts_.height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
      return;
    }
    sdl_initialized_ = true;

    bool attributes_ok = true;
    attributes_ok &= set_gl_attribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3,
                                      "CONTEXT_MAJOR_VERSION");
    attributes_ok &= set_gl_attribute(SDL_GL_CONTEXT_MINOR_VERSION, 3,
                                      "CONTEXT_MINOR_VERSION");
    attributes_ok &=
        set_gl_attribute(SDL_GL_CONTEXT_PROFILE_MASK,
                         SDL_GL_CONTEXT_PROFILE_CORE, "CONTEXT_PROFILE_MASK");
#ifdef __APPLE__
    attributes_ok &= set_gl_attribute(SDL_GL_CONTEXT_FLAGS,
                                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG,
                                      "CONTEXT_FLAGS");
#endif
    attributes_ok &= set_gl_attribute(SDL_GL_RED_SIZE, 8, "RED_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_GREEN_SIZE, 8, "GREEN_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_BLUE_SIZE, 8, "BLUE_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_ALPHA_SIZE, 8, "ALPHA_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_DEPTH_SIZE, 24, "DEPTH_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_STENCIL_SIZE, 8, "STENCIL_SIZE");
    attributes_ok &= set_gl_attribute(SDL_GL_DOUBLEBUFFER, 1, "DOUBLEBUFFER");
    attributes_ok &=
        set_gl_attribute(SDL_GL_MULTISAMPLEBUFFERS, 0, "MULTISAMPLEBUFFERS");
    attributes_ok &=
        set_gl_attribute(SDL_GL_MULTISAMPLESAMPLES, 0, "MULTISAMPLESAMPLES");
    if (!attributes_ok) {
      return;
    }

    const std::string title =
        std::string(workload_title(workload_)) + " NanoVG (OpenGL 3)";
    window_ = SDL_CreateWindow(
        title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        static_cast<int>(opts_.width), static_cast<int>(opts_.height),
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return;
    }

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
      std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
      return;
    }
    if (SDL_GL_MakeCurrent(window_, context_) < 0) {
      std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << "\n";
      return;
    }

    const int requested_swap_interval = opts_.vsync ? 1 : 0;
    if (SDL_GL_SetSwapInterval(requested_swap_interval) < 0) {
      std::cerr << "Unable to set swap interval to " << requested_swap_interval
                << ": " << SDL_GetError() << "\n";
      return;
    }
    actual_swap_interval_ = SDL_GL_GetSwapInterval();
    if (actual_swap_interval_ != requested_swap_interval) {
      std::cerr << "Requested swap interval " << requested_swap_interval
                << ", but SDL reports " << actual_swap_interval_ << "\n";
      return;
    }

    const auto dimensions = bench::adjust_window_for_hidpi(
        window_, opts_.width, opts_.height, true, nullptr);
    if (dimensions.drawable_w <= 0 || dimensions.drawable_h <= 0) {
      std::cerr << "Unable to determine the OpenGL drawable size\n";
      return;
    }
    width_ = static_cast<uint32_t>(dimensions.drawable_w);
    height_ = static_cast<uint32_t>(dimensions.drawable_h);
    if (width_ != opts_.width || height_ != opts_.height) {
      std::cerr << "Requested drawable " << opts_.width << "x" << opts_.height
                << ", but created " << width_ << "x" << height_ << "\n";
      return;
    }

    if (!bench::verify_gl33_core_context("NanoVG")) {
      return;
    }

    vg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg_) {
      std::cerr << "nvgCreateGL3 failed\n";
      return;
    }

    const auto *renderer = glGetString(GL_RENDERER);
    const auto *vendor = glGetString(GL_VENDOR);
    const auto *version = glGetString(GL_VERSION);
    gpu_device_ =
        renderer ? reinterpret_cast<const char *>(renderer) : "unknown";
    gpu_vendor_ = vendor ? reinterpret_cast<const char *>(vendor) : "unknown";
    gpu_driver_ = version ? reinterpret_cast<const char *>(version) : "unknown";
    graphics_api_ = "OpenGL " + gpu_driver_;
  }

  NanoVgWindow(const NanoVgWindow &) = delete;
  NanoVgWindow &operator=(const NanoVgWindow &) = delete;

  ~NanoVgWindow() override {
    if (vg_) {
      nvgDeleteGL3(vg_);
    }
    if (context_) {
      SDL_GL_DeleteContext(context_);
    }
    if (window_) {
      SDL_DestroyWindow(window_);
    }
    if (sdl_initialized_) {
      SDL_Quit();
    }
  }

  bool ready() override {
    if (!vg_) {
      return false;
    }

    switch (workload_) {
    case Workload::Circle:
    case Workload::RadialGradient: {
      bench::CircleGenConfig config;
      config.canvas_width = width_;
      config.canvas_height = height_;
      circles_ = bench::generate_static_circles(opts_.seed, config);
      break;
    }
    case Workload::Rect:
    case Workload::Stroke:
    case Workload::Image:
    case Workload::LinearGradient:
    case Workload::StrokeRect: {
      bench::RectGenConfig config;
      config.canvas_width = width_;
      config.canvas_height = height_;
      rects_ = bench::generate_static_rects(opts_.seed, config);
      break;
    }
    }

    if (workload_ == Workload::Image) {
      const auto image_path = find_image_path(opts_.image_ext);
      if (image_path.empty()) {
        std::cerr << "Unable to find test." << opts_.image_ext << "\n";
        return false;
      }
      asset_hash_ = bench::sha256_file(image_path);
      if (asset_hash_.empty()) {
        std::cerr << "Failed to hash image asset: " << image_path << "\n";
        return false;
      }
      image_ = nvgCreateImage(vg_, image_path.c_str(), 0);
      if (image_ == 0) {
        std::cerr << "NanoVG failed to decode " << image_path << "\n";
        return false;
      }
      nvgImageSize(vg_, image_, &image_width_, &image_height_);
      if (image_width_ <= 0 || image_height_ <= 0) {
        std::cerr << "NanoVG reported an invalid image size\n";
        return false;
      }
    }

    // Match the other C++ adapters: leave a prepared frame in the back buffer
    // so --capture can run before the warmup loop.
    return update(0) && draw();
  }

  bool update(uint32_t frame_index) override {
    bench::TransformGenConfig transform_config;
    if (opts_.scene_mode == bench::SceneMode::Default ||
        workload_ == Workload::Circle ||
        workload_ == Workload::RadialGradient) {
      transform_config.max_rotation_deg = 0.0f;
    }

    switch (workload_) {
    case Workload::Circle:
    case Workload::RadialGradient:
      transforms_ = bench::generate_transforms(
          opts_.seed, frame_index, static_cast<uint32_t>(circles_.size()),
          transform_config);
      break;
    case Workload::Rect:
    case Workload::Stroke:
    case Workload::Image:
    case Workload::LinearGradient:
    case Workload::StrokeRect:
      transforms_ = bench::generate_transforms(
          opts_.seed, frame_index, static_cast<uint32_t>(rects_.size()),
          transform_config);
      break;
    }
    return true;
  }

  bool draw() override {
    if (!vg_) {
      return false;
    }

    glViewport(0, 0, static_cast<GLsizei>(width_),
               static_cast<GLsizei>(height_));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg_, static_cast<float>(width_), static_cast<float>(height_),
                  1.0f);
    nvgMiterLimit(vg_, 4.0f);

    switch (workload_) {
    case Workload::Rect:
      draw_rects(false, false);
      break;
    case Workload::Circle:
      draw_circles(false);
      break;
    case Workload::Stroke:
      draw_rects(false, true);
      break;
    case Workload::Image:
      draw_images();
      break;
    case Workload::LinearGradient:
      draw_linear_gradients();
      break;
    case Workload::RadialGradient:
      draw_circles(true);
      break;
    case Workload::StrokeRect:
      draw_rects(true, false);
      break;
    }

    nvgEndFrame(vg_);
    return true;
  }

  void refresh() override {
    if (window_) {
      SDL_GL_SwapWindow(window_);
    }
  }

  bool finish_gpu() override {
    if (opts_.gpu_sync) {
      glFinish();
    }
    return true;
  }

  bool capture(const std::string &path) override {
    if (!ends_with(path, ".ppm")) {
      std::cerr << "NanoVG capture requires a .ppm output path: " << path
                << "\n";
      return false;
    }
    if (!context_ || SDL_GL_MakeCurrent(window_, context_) < 0) {
      std::cerr << "Unable to make the NanoVG context current for capture\n";
      return false;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * height_ * 4u);
    GLint previous_pack_alignment = 0;
    GLint previous_read_buffer = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
    glGetIntegerv(GL_READ_BUFFER, &previous_read_buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, static_cast<GLsizei>(width_),
                 static_cast<GLsizei>(height_), GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    const GLenum read_error = glGetError();
    glReadBuffer(static_cast<GLenum>(previous_read_buffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
    if (read_error != GL_NO_ERROR) {
      std::cerr << "glReadPixels failed with GL error 0x" << std::hex
                << read_error << std::dec << "\n";
      return false;
    }

    return bench::write_ppm_rgba(path, pixels.data(), width_, height_,
                                 static_cast<size_t>(width_) * 4u, true);
  }

  uint32_t surface_width() const override { return width_; }
  uint32_t surface_height() const override { return height_; }

  const char *engine_id() const override { return "nanovg"; }
  const char *engine_title() const override { return "NanoVG"; }
  const char *backend_id() const override { return "gl"; }
  const char *backend_title() const override { return "OpenGL"; }
  const char *engine_version() const override {
    return VGBENCH_NANOVG_VERSION;
  }
  const char *engine_revision() const override {
    return VGBENCH_NANOVG_REVISION;
  }
  const char *scene_model() const override { return "immediate"; }
  const char *graphics_api() const override { return graphics_api_.c_str(); }
  const char *gpu_device() const override { return gpu_device_.c_str(); }
  const char *gpu_vendor() const override { return gpu_vendor_.c_str(); }
  const char *gpu_driver() const override { return gpu_driver_.c_str(); }
  const char *gpu_completion() const override {
    return opts_.gpu_sync ? "glFinish" : "none";
  }
  const char *present_mode() const override {
    return actual_swap_interval_ == 0 ? "immediate" : "fifo";
  }
  const char *pixel_format() const override { return "RGBA8"; }
  const char *asset_hash() const override { return asset_hash_.c_str(); }
  bool vsync_actual() const override { return actual_swap_interval_ != 0; }
  bool vsync_verified() const override {
    return actual_swap_interval_ == (opts_.vsync ? 1 : 0);
  }

private:
  void begin_transformed_shape(size_t index, float cx, float cy) {
    nvgSave(vg_);
    const auto affine = bench::centered_transform(transforms_[index], cx, cy);
    // NanoVG stores [a c e; b d f], whereas Affine2D is row-major.
    nvgTransform(vg_, affine.a, affine.c, affine.b, affine.d, affine.tx,
                 affine.ty);
  }

  void end_transformed_shape() { nvgRestore(vg_); }

  void draw_rects(bool fill_and_stroke, bool stroke_only) {
    for (size_t i = 0; i < rects_.size(); ++i) {
      const auto &rect = rects_[i];
      const float cx = rect.x + rect.w * 0.5f;
      const float cy = rect.y + rect.h * 0.5f;
      begin_transformed_shape(i, cx, cy);

      nvgBeginPath(vg_);
      nvgRect(vg_, rect.x, rect.y, rect.w, rect.h);
      if (!stroke_only) {
        nvgFillColor(vg_, rect_color(rect));
        nvgFill(vg_);
      }
      if (stroke_only || fill_and_stroke) {
        nvgStrokeColor(vg_, fill_and_stroke ? nvgRGBA(255, 255, 255, 255)
                                            : rect_color(rect));
        nvgStrokeWidth(vg_, fill_and_stroke ? 3.0f
                                            : 3.0f + (rect.w + rect.h) * 0.02f);
        nvgStroke(vg_);
      }

      end_transformed_shape();
    }
  }

  void draw_circles(bool radial_gradient) {
    for (size_t i = 0; i < circles_.size(); ++i) {
      const auto &circle = circles_[i];
      begin_transformed_shape(i, circle.cx, circle.cy);

      nvgBeginPath(vg_);
      nvgCircle(vg_, circle.cx, circle.cy, circle.radius);
      const NVGcolor inner = circle_color(circle);
      if (radial_gradient) {
        const NVGpaint paint =
            nvgRadialGradient(vg_, circle.cx, circle.cy, 0.0f, circle.radius,
                              inner, complement(inner));
        nvgFillPaint(vg_, paint);
      } else {
        nvgFillColor(vg_, inner);
      }
      nvgFill(vg_);

      end_transformed_shape();
    }
  }

  void draw_linear_gradients() {
    for (size_t i = 0; i < rects_.size(); ++i) {
      const auto &rect = rects_[i];
      const float cx = rect.x + rect.w * 0.5f;
      const float cy = rect.y + rect.h * 0.5f;
      begin_transformed_shape(i, cx, cy);

      const NVGcolor start = rect_color(rect);
      const NVGpaint paint =
          nvgLinearGradient(vg_, rect.x, rect.y, rect.x + rect.w,
                            rect.y + rect.h, start, complement(start));
      nvgBeginPath(vg_);
      nvgRect(vg_, rect.x, rect.y, rect.w, rect.h);
      nvgFillPaint(vg_, paint);
      nvgFill(vg_);

      end_transformed_shape();
    }
  }

  void draw_images() {
    const float image_w = static_cast<float>(image_width_);
    const float image_h = static_cast<float>(image_height_);
    for (size_t i = 0; i < rects_.size(); ++i) {
      const auto &rect = rects_[i];
      const float fit_scale = std::min(rect.w / image_w, rect.h / image_h);
      const float cx = rect.x + rect.w * 0.5f;
      const float cy = rect.y + rect.h * 0.5f;
      const auto affine = bench::centered_transform(
          transforms_[i], cx, cy, image_w * 0.5f, image_h * 0.5f, fit_scale);

      nvgSave(vg_);
      nvgTransform(vg_, affine.a, affine.c, affine.b, affine.d, affine.tx,
                   affine.ty);
      const NVGpaint paint = nvgImagePattern(vg_, 0.0f, 0.0f, image_w, image_h,
                                             0.0f, image_, 1.0f);
      nvgBeginPath(vg_);
      nvgRect(vg_, 0.0f, 0.0f, image_w, image_h);
      nvgFillPaint(vg_, paint);
      nvgFill(vg_);
      nvgRestore(vg_);
    }
  }

  bench::CliOptions opts_;
  Workload workload_;
  SDL_Window *window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  NVGcontext *vg_ = nullptr;
  bool sdl_initialized_ = false;
  int actual_swap_interval_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;

  std::vector<bench::RectData> rects_;
  std::vector<bench::CircleData> circles_;
  std::vector<bench::TransformData> transforms_;

  std::string asset_hash_;
  int image_ = 0;
  int image_width_ = 0;
  int image_height_ = 0;

  std::string graphics_api_ = "OpenGL";
  std::string gpu_device_ = "unknown";
  std::string gpu_vendor_ = "unknown";
  std::string gpu_driver_ = "unknown";
};

} // namespace

const char *workload_id(Workload workload) {
  switch (workload) {
  case Workload::Rect:
    return "rect";
  case Workload::Circle:
    return "circle";
  case Workload::Stroke:
    return "stroke";
  case Workload::Image:
    return "image";
  case Workload::LinearGradient:
    return "lineargradient";
  case Workload::RadialGradient:
    return "radialgradient";
  case Workload::StrokeRect:
    return "strokerect";
  }
  return "unknown";
}

const char *workload_title(Workload workload) {
  switch (workload) {
  case Workload::Rect:
    return "Rectbench";
  case Workload::Circle:
    return "Circlebench";
  case Workload::Stroke:
    return "Strokebench";
  case Workload::Image:
    return "Imagebench";
  case Workload::LinearGradient:
    return "Linear Gradient Bench";
  case Workload::RadialGradient:
    return "Radial Gradient Bench";
  case Workload::StrokeRect:
    return "StrokeRectbench";
  }
  return "Benchmark";
}

int run(int argc, char *argv[], Workload workload) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);
  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }
  if (opts.backend != bench::Backend::GL) {
    std::cerr << "Error: NanoVG supports only --backend=gl\n";
    return 1;
  }
  if (!opts.benchmark.empty() && opts.benchmark != workload_id(workload)) {
    std::cerr << "Error: this executable implements benchmark '"
              << workload_id(workload) << "', not '" << opts.benchmark << "'\n";
    return 1;
  }
  opts.benchmark = workload_id(workload);

  std::cout << workload_title(workload) << " NanoVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";
  std::cout << "GPU sync:" << (opts.gpu_sync ? " glFinish" : " disabled")
            << "\n";

  NanoVgWindow window(opts, workload);
  return bench::run_benchmark(opts, window);
}

} // namespace bench::nanovgbench
