#pragma once

namespace bench::nanovgbench {

enum class Workload {
  Rect,
  Circle,
  Stroke,
  Image,
  LinearGradient,
  RadialGradient,
  StrokeRect,
};

const char *workload_id(Workload workload);
const char *workload_title(Workload workload);

int run(int argc, char *argv[], Workload workload);

} // namespace bench::nanovgbench
