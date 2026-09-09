/**
 * Multi-image benchmark: Skia SDL benchmark
 *
 * Draws 5000 instances selected from 25 independently loaded images.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "multi_image_layout.hpp"
#include "skia_sdl_example.hpp"

#include "core/SkData.h"
#include "core/SkImage.h"
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkSamplingOptions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string find_image_path(const char *asset) {
  const std::string paths[] = {
      "../resource/image/" + std::string(asset),
      "resource/image/" + std::string(asset),
      "./resource/image/" + std::string(asset),
  };
  for (const auto &path : paths) {
    FILE *file = fopen(path.c_str(), "r");
    if (file) {
      fclose(file);
      return path;
    }
  }
  return paths[0];
}

sk_sp<SkImage> load_image(const std::string &path) {
  auto data = SkData::MakeFromFileName(path.c_str());
  if (!data)
    return nullptr;
  return SkImages::DeferredFromEncodedData(data);
}

class MultiImagebenchExample final : public bench::skiaexam::Example {
public:
  explicit MultiImagebenchExample(bench::CliOptions opts)
      : opts_(std::move(opts)) {}

  bool content(SkCanvas *canvas, uint32_t width, uint32_t height) override {
    (void)canvas;
    positions_ = bench::generate_multi_image_layout(opts_.seed, width, height);
    image_indices_ = bench::generate_multi_image_indices(
        opts_.seed, static_cast<uint32_t>(positions_.size()));

    images_.reserve(bench::kMultiImageAssets.size());
    for (const char *asset : bench::kMultiImageAssets) {
      const std::string path = find_image_path(asset);
      auto image = load_image(path);
      if (!image) {
        std::cerr << "Failed to load image: " << path << "\n";
        return false;
      }
      images_.push_back(std::move(image));
    }
    transforms_.resize(positions_.size(), {0.0f, 0.0f, 0.0f, 1.0f});
    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    bench::TransformGenConfig config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      config.max_rotation_deg = 0.0f;
    }
    transforms_ = bench::generate_transforms(
        opts_.seed, elapsed, static_cast<uint32_t>(positions_.size()), config);
    return true;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas)
      return false;

    canvas->clear(SK_ColorBLACK);
    SkPaint paint;
    paint.setAntiAlias(true);
    const SkSamplingOptions sampling(SkFilterMode::kLinear);

    for (size_t i = 0; i < positions_.size(); ++i) {
      const auto &image = images_[image_indices_[i]];
      const auto &position = positions_[i];
      const auto &transform = transforms_[i];
      const float image_width = static_cast<float>(image->width());
      const float image_height = static_cast<float>(image->height());
      const float fit_scale =
          std::min(position.w / image_width, position.h / image_height);
      const float scale = fit_scale * transform.scale;
      const float radians = transform.rotation_deg * 0.01745329251994329577f;
      const float cosine = std::cos(radians) * scale;
      const float sine = std::sin(radians) * scale;
      const float center_x = position.x + position.w * 0.5f;
      const float center_y = position.y + position.h * 0.5f;
      const float tx = transform.dx + center_x -
                       (cosine * image_width - sine * image_height) * 0.5f;
      const float ty = transform.dy + center_y -
                       (sine * image_width + cosine * image_height) * 0.5f;

      SkMatrix matrix;
      matrix.setAll(cosine, -sine, tx, sine, cosine, ty, 0, 0, 1);
      canvas->save();
      canvas->concat(matrix);
      paint.setAlpha(position.a);
      canvas->drawImage(image, 0, 0, sampling, &paint);
      canvas->restore();
    }
    return true;
  }

private:
  bench::CliOptions opts_;
  std::vector<bench::RectData> positions_;
  std::vector<uint32_t> image_indices_;
  std::vector<sk_sp<SkImage>> images_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<MultiImagebenchExample>(opts);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "MultiImagebench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "MultiImagebench");
  case bench::Backend::WebGPU:
    std::cerr << "Error: Skia WebGPU backend not implemented\n";
    return nullptr;
  }
  return nullptr;
}

} // namespace

int main(int argc, char *argv[]) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);
  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }

  std::cout << "MultiImagebench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Images:  " << bench::kMultiImageCount
            << " instances from 25 assets\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->window || !window->example) {
    return 1;
  }
  return bench::run_benchmark(opts, *window);
}
