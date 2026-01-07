/**
 * Imagebench: ThorVG SDL Benchmark
 *
 * Draws multiple instances of test.jpg or test.png at random positions.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"

#include "tvg_sdl_example.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// Try multiple paths to locate the resource (works from build/ or project root)
constexpr const char *kImageBaseName = "test";

std::string image_filename(const std::string &image_ext) {
  return std::string(kImageBaseName) + "." + image_ext;
}

std::string find_image_path(const std::string &image_ext) {
  const std::string filename = image_filename(image_ext);
  const std::string kPaths[] = {
    "../resource/image/" + filename, // From build/ directory
    "resource/image/" + filename,    // From project root
    "./resource/image/" + filename,  // Explicit current dir
  };
  for (const auto &path : kPaths) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
      fclose(f);
      return path;
    }
  }
  return kPaths[0];  // Return default even if not found (let error happen later)
}

class ImagebenchExample final : public bench::tvgexam::Example {
public:
  explicit ImagebenchExample(bench::CliOptions opts)
      : opts_(std::move(opts)) {}

  bool content(tvg::Canvas *canvas, uint32_t w, uint32_t h) override {
    rect_config_.canvas_width = w;
    rect_config_.canvas_height = h;

    // Generate positions (reusing rect generator for positions/sizes)
    static_positions_ = bench::generate_static_rects(opts_.seed, rect_config_);

    // Load the base image once to get dimensions
    const std::string image_path = find_image_path(opts_.image_ext);
    auto base_pic = tvg::Picture::gen();
    if (base_pic->load(image_path.c_str()) != tvg::Result::Success) {
      std::cerr << "Failed to load image: " << image_path << "\n";
      return false;
    }

    float img_w, img_h;
    base_pic->size(&img_w, &img_h);
    img_width_ = img_w;
    img_height_ = img_h;

    // Create picture instances for each position
    static_pictures_.reserve(static_positions_.size());
    scaled_sizes_.reserve(static_positions_.size());
    for (const auto &pos : static_positions_) {
      auto pic = tvg::Picture::gen();
      if (pic->load(image_path.c_str()) != tvg::Result::Success) {
        continue;
      }

      // Pre-scale to fit within bounds (same as Skia's base_scale)
      const float scale_x = pos.w / img_width_;
      const float scale_y = pos.h / img_height_;
      const float base_scale = std::min(scale_x, scale_y);
      
      const float scaled_w = img_width_ * base_scale;
      const float scaled_h = img_height_ * base_scale;
      pic->size(scaled_w, scaled_h);
      
      // Store scaled sizes for update()
      scaled_sizes_.push_back({scaled_w, scaled_h});

      // Initial position
      tvg::Matrix m = {1, 0, pos.x, 0, 1, pos.y, 0, 0, 1};
      pic->transform(m);

      static_pictures_.push_back(pic);
      canvas->push(pic);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    if (opts_.scene_mode == bench::SceneMode::Default ||
        opts_.scene_mode == bench::SceneMode::Rotation) {
      auto transforms = bench::generate_transforms(
          opts_.seed, frame_index, rect_config_.rect_count, transform_config);

      constexpr float kDegToRad = 0.01745329251994329576923690768489f;

      for (size_t i = 0; i < static_pictures_.size() && i < transforms.size() &&
                         i < static_positions_.size() && i < scaled_sizes_.size();
           ++i) {
        const auto &t = transforms[i];
        const auto &pos = static_positions_[i];
        const auto &sz = scaled_sizes_[i];

        // Image is already pre-scaled, so only apply t.scale on top
        const float cx = pos.x + pos.w * 0.5f;
        const float cy = pos.y + pos.h * 0.5f;

        float a, b, c, d;
        if (t.rotation_deg == 0.0f) {
          a = t.scale;
          b = 0.0f;
          c = 0.0f;
          d = t.scale;
        } else {
          const float rad = t.rotation_deg * kDegToRad;
          const float cos_theta = std::cos(rad);
          const float sin_theta = std::sin(rad);

          a = cos_theta * t.scale;
          b = -sin_theta * t.scale;
          c = sin_theta * t.scale;
          d = cos_theta * t.scale;
        }

        // Use the pre-scaled size for centering calculation
        const float tx = t.dx + cx - (a * sz.w * 0.5f + b * sz.h * 0.5f);
        const float ty = t.dy + cy - (c * sz.w * 0.5f + d * sz.h * 0.5f);

        tvg::Matrix m = {a, b, tx, c, d, ty, 0, 0, 1};
        static_pictures_[i]->transform(m);
      }

      canvas->update();
      return true;
    }
    return false;
  }

private:
  struct ScaledSize {
    float w, h;
  };
  
  bench::CliOptions opts_;
  bench::RectGenConfig rect_config_{};
  float img_width_ = 0;
  float img_height_ = 0;
  std::vector<bench::RectData> static_positions_;
  std::vector<tvg::Picture *> static_pictures_;
  std::vector<ScaledSize> scaled_sizes_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<ImagebenchExample>(opts);

  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Imagebench", tvg::EngineOption::None);

  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Imagebench");

  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.threads,
        opts.wgpu_external_device, "Imagebench");
  }

  return nullptr;
}

int run_benchmark(const bench::CliOptions &opts) {
  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->canvas || !window->example) {
    return 1;
  }

  window->clearBuffer = true;

  return bench::run_benchmark(opts, *window);
}

} // namespace

int main(int argc, char *argv[]) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);

  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }

  std::cout << "Imagebench ThorVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Image:   " << image_filename(opts.image_ext) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}
