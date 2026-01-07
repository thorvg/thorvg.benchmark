/**
 * Imagebench: Skia SDL Benchmark
 *
 * Draws multiple instances of test.jpg or test.png at random positions.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"
#include "skia_sdl_example.hpp"

// Skia headers
#include "core/SkData.h"
#include "core/SkImage.h"
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkSamplingOptions.h"

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

// Helper to load image file
sk_sp<SkImage> load_image(const std::string &path) {
  sk_sp<SkData> data = SkData::MakeFromFileName(path.c_str());
  if (!data) {
    std::cerr << "Failed to load image data from: " << path << "\n";
    return nullptr;
  }
  sk_sp<SkImage> image = SkImages::DeferredFromEncodedData(data);
  if (!image) {
    std::cerr << "Failed to decode image: " << path << "\n";
    return nullptr;
  }
  return image;
}

void draw_images_skia(
    SkCanvas *canvas, sk_sp<SkImage> image,
    const std::vector<bench::RectData> &positions,
    const std::vector<bench::TransformData> *transforms = nullptr) {
  if (!image) return;

  SkPaint paint;
  paint.setAntiAlias(true);
  const SkSamplingOptions sampling(SkFilterMode::kNearest);

  const bool apply_transforms =
      transforms && transforms->size() >= positions.size();

  constexpr float kDegToRad = 0.01745329251994329576923690768489f;

  for (size_t i = 0; i < positions.size(); ++i) {
    const auto &pos = positions[i];

    // Scale image to fit within rect bounds
    const float img_w = static_cast<float>(image->width());
    const float img_h = static_cast<float>(image->height());
    const float scale_x = pos.w / img_w;
    const float scale_y = pos.h / img_h;
    const float scale = std::min(scale_x, scale_y);

    if (!apply_transforms) {
      canvas->save();
      canvas->translate(pos.x, pos.y);
      canvas->scale(scale, scale);
      canvas->drawImage(image, 0, 0, sampling, &paint);
      canvas->restore();
      continue;
    }

    const auto &t = (*transforms)[i];

    const float cx = pos.x + pos.w * 0.5f;
    const float cy = pos.y + pos.h * 0.5f;

    float a, b, c, d;
    if (t.rotation_deg == 0.0f) {
      a = t.scale * scale;
      b = 0.0f;
      c = 0.0f;
      d = t.scale * scale;
    } else {
      const float rad = t.rotation_deg * kDegToRad;
      const float cos_theta = std::cos(rad);
      const float sin_theta = std::sin(rad);

      a = cos_theta * t.scale * scale;
      b = -sin_theta * t.scale * scale;
      c = sin_theta * t.scale * scale;
      d = cos_theta * t.scale * scale;
    }

    const float tx = t.dx + cx - (a * img_w * 0.5f + b * img_h * 0.5f);
    const float ty = t.dy + cy - (c * img_w * 0.5f + d * img_h * 0.5f);

    SkMatrix m;
    m.setAll(a, b, tx, c, d, ty, 0, 0, 1);

    canvas->save();
    canvas->concat(m);
    canvas->drawImage(image, 0, 0, sampling, &paint);
    canvas->restore();
  }
}

class ImagebenchExample final : public bench::skiaexam::Example {
public:
  ImagebenchExample(uint64_t seed, bench::SceneMode scene_mode,
                    std::string image_ext)
      : seed_(seed), scene_mode_(scene_mode),
        image_ext_(std::move(image_ext)) {}

  bool content(SkCanvas *canvas, uint32_t w, uint32_t h) override {
    (void)canvas;
    rect_config_.canvas_width = w;
    rect_config_.canvas_height = h;

    // Load the image
    image_path_ = find_image_path(image_ext_);
    image_ = load_image(image_path_);
    if (!image_) {
      std::cerr << "Failed to load image: " << image_path_ << "\n";
      return false;
    }

    // Generate positions (reusing rect generator for positions/sizes)
    static_positions_ = bench::generate_static_rects(seed_, rect_config_);

    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    if (scene_mode_ == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ = bench::generate_transforms(seed_, frame_index,
                                               rect_config_.rect_count,
                                               transform_config);
      return true;
    }
    return false;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas || !image_) {
      return false;
    }

    canvas->clear(SK_ColorBLACK);

    const std::vector<bench::TransformData> *transforms_ptr = nullptr;

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ptr = &transforms_;
    }

    draw_images_skia(canvas, image_, static_positions_, transforms_ptr);
    return true;
  }

private:
  uint64_t seed_ = 0;
  bench::SceneMode scene_mode_ = bench::SceneMode::Default;
  std::string image_ext_;
  std::string image_path_;
  bench::RectGenConfig rect_config_{};
  sk_sp<SkImage> image_;
  std::vector<bench::RectData> static_positions_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<ImagebenchExample>(
      opts.seed, opts.scene_mode, opts.image_ext);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "Imagebench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "Imagebench");
  case bench::Backend::WebGPU:
    std::cerr << "Error: Skia WebGPU backend not implemented\n";
    return nullptr;
  }

  return nullptr;
}

int run_benchmark(const bench::CliOptions &opts) {
  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->window || !window->example) {
    return 1;
  }
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

  std::cout << "Imagebench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Image:   " << image_filename(opts.image_ext) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}
