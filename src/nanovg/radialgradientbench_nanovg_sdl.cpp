#include "nanovg_bench.hpp"

int main(int argc, char *argv[]) {
  return bench::nanovgbench::run(argc, argv,
                                 bench::nanovgbench::Workload::RadialGradient);
}
