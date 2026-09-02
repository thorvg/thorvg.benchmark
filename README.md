[![License](https://img.shields.io/badge/licence-MIT-green.svg?style=flat)](LICENSE)
[![Wikipedia](https://img.shields.io/badge/Wikipedia-000000?style=flat&logo=wikipedia&logoColor=white)](https://en.wikipedia.org/wiki/Thor_Vector_Graphics)
[![Discord](https://img.shields.io/badge/Community-5865f2?style=flat&logo=discord&logoColor=white)](https://discord.gg/n25xj6J6HM)
[![OpenCollective](https://img.shields.io/badge/OpenCollective-84B5FC?style=flat&logo=opencollective&logoColor=white)](https://opencollective.com/thorvg)

# ThorVG Benchmark

<p align="center">
  <img width="550" height="auto" src="https://github.com/thorvg/thorvg.site/blob/main/readme/logo/animated_brand.svg">
</p>

This repository compares primitive microbenchmarks across Skia, ThorVG,
NanoVG, Pathfinder, and Vello. Skia and ThorVG provide eight workloads; NanoVG,
Pathfinder, and Vello support the original seven workload IDs.

| Engine | Backend | Scene model | Dependency |
|---|---|---|---|
| Skia | CPU, OpenGL | immediate | configured vcpkg package |
| ThorVG | CPU, OpenGL, WebGPU | retained | installed package or latest `main` source build |
| NanoVG | OpenGL 3.3 core | immediate | configured vcpkg package |
| Pathfinder | OpenGL GL3/D3D9 | immediate | crates.io `0.5` packages |
| Vello | WebGPU/wgpu | immediate | crates.io `0.10.0` package |

Skia and ThorVG support `rect`, `circle`, `stroke`, `image`, `multiimage`,
`lineargradient`, `radialgradient`, and `strokerect`, each in `default` and
`rotation` scene modes. The added competitors support these IDs except
`multiimage`. Their `stroke` workload retains the original rectangle strokes;
Skia and ThorVG use `designer-strokes-v1`, described below, so these stroke
results are not directly comparable. SVG, text, clipping, and full animation
workloads are outside this suite.

## Timing and comparability

WebGPU samples measure:

```text
update -> encode/render/submit -> present -> explicit GPU completion
```

OpenGL preserves main's asynchronous timing: update, rendering submission, and
presentation without an explicit GPU completion wait. These measurements are
labelled submission timings and excluded from synchronized GPU rankings.
WebGPU waits for submitted queue work after presentation.

OpenGL adapters request a 3.3 core context, RGBA8, depth24, stencil8, double
buffering, and no MSAA. They query the actual swap interval. WebGPU adapters
prefer Immediate; Mailbox and FIFO results are marked `vsync-limited`. Only
schema-v2 runs with an exact drawable size, verified non-limiting presentation,
complete raw samples, explicit GPU completion, and a Release build are
comparable. ThorVG defaults to four worker threads; its C++ adapters also
accept `--threads=INT|max`.
Image results record a runtime SHA-256 of the exact asset path selected by the
adapter; the suite rejects it if it differs from the manifest's canonical hash.
Within each workload, scene, and API, the suite ranks GPU results only when
every repeat and engine reports the same GPU device and pixel format. A change
in either keeps the measurements for diagnostics but excludes the group from
performance conclusions.

Adapters use a black background, source-over blending, antialiasing,
bilinear image sampling, clamped/padded gradients, and the same center-based
scale/rotate/translate order. Rectangle strokes use butt caps with miter joins
and miter limit 4; Skia and ThorVG designer strokes use round caps and joins.
The ThorVG image adapter relies on ThorVG's fixed linear sampler because its public
`Picture` API has no per-picture filtering switch.

## Build

Requirements are CMake 3.20+, Ninja, SDL3 for C++ adapters, and the SDK for
the selected engine. Rust engines require Rust 1.88 or newer. Skia and NanoVG use vcpkg packages;
Pathfinder and Vello use crates.io packages resolved by their adapter lockfiles.
ThorVG and its WebGPU adapter require wgpu-native. By default ThorVG is loaded
from the installed `thorvg-1` pkg-config package. Set
`VGBENCH_BUILD_THORVG_FROM_SOURCE=ON` to fetch and build the latest ThorVG
`main` branch instead. A normal CMake configure builds only the ThorVG
adapters; every competitor is opt-in.

Each engine can be built without discovering unrelated dependencies:

```bash
# NanoVG only
cmake --preset nanovg-only
cmake --build --preset nanovg-only

# Pathfinder only (Cargo always uses --locked)
cmake --preset pathfinder-only
cmake --build --preset pathfinder-only

# Vello only
cmake --preset vello-only
cmake --build --preset vello-only
```

The `all-gpu` preset enables all five engine switches. When Skia or NanoVG is
enabled, provide the vcpkg toolchain through your environment or configure
command. Equivalent switches are:

```text
VGBENCH_ENABLE_SKIA
VGBENCH_ENABLE_THORVG
VGBENCH_ENABLE_NANOVG
VGBENCH_ENABLE_PATHFINDER
VGBENCH_ENABLE_VELLO
VGBENCH_ENABLE_ALL_GPU
```

## Run one benchmark

C++ adapters use one executable per workload. Rust adapters combine all
workloads behind `--benchmark`:

```bash
./build/rect_skia_sdl --backend=gl --frames=100 --warmup=10
./build/rect_tvg_sdl --backend=gl --frames=100 --warmup=10
./build/multiimage_skia_sdl --backend=cpu
./build/multiimage_tvg_sdl --backend=cpu

./build/nanovg-only/rectbench_nanovg_sdl \
  --backend=gl --scene=rotation --frames=100 --warmup=10

./build/pathfinder-only/pathfinder-bench \
  --benchmark=rect --backend=gl --scene=rotation

./build/vello-only/vello-bench \
  --benchmark=rect --backend=webgpu --scene=rotation
```

Shared arguments include:

| Option | Default | Meaning |
|---|---:|---|
| `--benchmark=ID` | inferred / `rect` | workload for combined Rust binaries |
| `--backend=cpu\|gl\|webgpu` | engine default | backend |
| `--scene=default\|rotation` | `default` | scene mode |
| `--seed=INT` | `12345` | deterministic PCG32 seed |
| `--frames=INT` | `1000` | measured frames |
| `--warmup=INT` | `120` | warmup frames |
| `--width=INT`, `--height=INT` | `2560`, `1440` | exact drawable resolution |
| `--vsync=0\|1` | `0` | requested synchronization |
| `--capture=PATH` | none | lossless correctness capture |
| `--opacity=FLOAT` | `1` | Skia/ThorVG designer stroke opacity from 0 to 1 |
| `--threads=INT\|max` | `4` | ThorVG C++ worker threads |
| `--output=PATH` | generated | schema-v2 JSON path |

## Skia and ThorVG workloads

The `multiimage` executables draw 5,000 image instances at generated
positions and sizes, selecting each instance from a fixed set of 25 byte-distinct
PNG assets in the Kenney Animal Pack. The image sequence is generated once from
`--seed` and is identical across Skia and ThorVG; the assets are reused while
rendering.
The multi-image scene uses alpha values from 253 through 255.

### Stroke workload

The `stroke` executables draw 5,000 paths in a repeating, nearly equal mix
of everyday design shapes:

| Shape | Geometry | Typical use |
|-------|----------|-------------|
| Smooth S-connector | Two cubic curves, open | Flow diagrams and curved connectors |
| Rounded elbow connector | Three straight runs and two cubic turns, open | Routed diagram connections |
| Speech-bubble outline | Four curved corners and a triangular tail, closed | Chat and annotation icons |

Both engines use the same seeded geometry, colors, positions, and stroke widths.
Layout boxes range from 48 to 200 px on each axis, with 2–8 px strokes and round
caps and joins. Paths are built once before timing; the existing `default`
translation/scale and `rotation` animation modes still apply.

These are ordinary stroked paths rather than rectangles, ovals, rounded
rectangles, or single lines. They avoid Skia's corresponding
[primitive-shape shortcuts](https://skia.googlesource.com/skia/+/refs/heads/main/src/gpu/ganesh/SurfaceDrawContext.cpp),
while allowing normal path rendering optimizations. `strokerect` remains
available as the rectangle-specific workload.

Use `--opacity=0..1` to set the same stroke opacity on every path (default: `1`).
Both engines round this to an 8-bit color alpha: `0.5` becomes `128/255`.
Changing opacity preserves geometry, colors, and animation for a given seed.
Opacity `0` is useful for a visibility check; it does not measure visible stroke
rendering work.

```bash
./build/stroke_skia_sdl --backend=cpu --opacity=1 --vsync=0
./build/stroke_tvg_sdl --backend=cpu --opacity=1 --vsync=0

./build/stroke_skia_sdl --backend=gl --scene=rotation --opacity=0.5 --vsync=0
./build/stroke_tvg_sdl --backend=gl --scene=rotation --opacity=0.5 --vsync=0
```

Stroke result metadata includes `"workload": "designer-strokes-v1"` and the
requested `opacity`. This workload replaces the old rectangle-only stroke
benchmark; its results are not directly comparable with those historical runs.

## Manifest-driven suite

Capabilities and executable commands live in `config/engines.json`. The runner
defaults to ThorVG and uses matched seeds and cyclic counterbalancing within
each workload/backend/scene group. Select competitors explicitly with
`--engines`:

```bash
python3 tools/run_all.py \
  --bin-dir=build/all-gpu --engines=all --backends=all \
  --runs=3 -- --frames=200 --warmup=20

python3 tools/generate_report.py results_suite_20260902_210000/summary.json \
  --output report.md
```

Missing output, early closure, partial runs, unsupported variants, and metadata
mismatches make the suite incomplete instead of silently aggregating data.
When passed through the suite, `--capture=PATH` is a base name; the runner adds
the variant, seed, and run number so captures cannot overwrite one another.
Reports build CPU, OpenGL, and WebGPU sections dynamically. Historical
schema-v1 files remain readable but are labelled legacy and excluded from
synchronized rankings; cross-API speed ratios are not generated by default.

## Validate results

Adapters can write lossless captures with `--capture`; inspect those before
comparing performance. Software GPU implementations are not comparable.

## Tests

The shared benchmark logic and Python tools can be tested without installing
Skia, SDL, ThorVG, or WebGPU. A C++20 compiler, CMake, and Python 3.10+ are enough:

```bash
cmake -S . -B build-tests -DRECTBENCH_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The suite checks statistics, seeded workloads, CLI validation, JSON output,
suite orchestration, reporting, and fairness in the actual benchmark loop.
A fake clock verifies warmup exclusion, exact frame budgets, and that update,
draw/synchronization, and presentation stay inside the timed interval. Failed
or interrupted runs must return an error without publishing partial results.

Adapter and factory tests compile selected, unchanged production method bodies
against recording API substitutes. They check both engines' submission and
presentation order and option forwarding in all 16 factories. Source-line
mapping attributes coverage to those method bodies; it does not measure the
substituted engine APIs, graphics initialization, or hardware behavior.
Use `-DBUILD_TESTING=OFF` to omit tests from a rendering benchmark build.

For coverage, CRAP scores, and a selected mutation sample, use GCC and its
matching `gcov`, with the quality tools installed in your Python environment:

```bash
python -m pip install -r tests/requirements-quality.txt
cmake -S . -B build-tests -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Debug \
  -DRECTBENCH_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON -DRECTBENCH_TEST_COVERAGE=ON
cmake --build build-tests
python tests/quality.py --build-dir build-tests
```

Run this quality command instead of the separate CTest command: it runs the
tests while collecting coverage. It writes reports to the ignored
`build-test-quality/report/` directory. `--cmake` and `--gcov` accept alternate
tool paths; `--coverage-only` and `--mutations-only` allow separate collection
and mutation runs when investigating failures. `--mutation-prefix fairness-`
selects the fairness faults without repeating unrelated mutation checks.

CRAP combines cyclomatic complexity from Lizard with measured executable-line
coverage: `CC² * (1 - coverage)³ + CC`. Scores above 30 are reported for follow-up.
Functions without instrumentation have a null score;
they are excluded from the reported coverage denominator.

`tests/mutations.json` and `tests/fairness_mutations.json` define explicit
behavior-changing faults. Each mutant
runs against an isolated source copy after that copy passes its baseline tests.
Survivors, compile errors, and timeouts fail the quality command; only failing
tests or crashes kill a valid mutant. This selected sample is not exhaustive
mutation coverage or proof of correctness. The `tests` GitHub workflow runs
these checks on pushes and pull requests and uploads the reports.

## Linux

Linux builds are validated via GitHub Actions. See [`.github/workflows/linux.yml`](.github/workflows/linux.yml) for the CI configuration.

### Dependencies (Ubuntu 22.04)

```bash
sudo apt-get install -y \
  cmake ninja-build pkg-config python3 meson curl unzip \
  autoconf autoconf-archive automake libtool libtool-bin \
  libgl1-mesa-dev libegl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libxfixes-dev

# vcpkg + Skia
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install skia sdl3 --triplet x64-linux

# thorvg (optionally checkout to a specific tag for testing)
git clone https://github.com/thorvg/thorvg.git
meson setup thorvg/build thorvg -Dloaders=all -Dengines=all
ninja -C thorvg/build install
```

### Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVGBENCH_ENABLE_SKIA=ON
cmake --build build -j
```

## Image Assets

The [Kenney Animal Pack](resource/image/kenney-animal-pack/README.md) provides
80 2D PNG assets (72 unique file contents) for image benchmarks. The
`multiimage` manifest uses 25 byte-distinct files from this pack and
selects them deterministically with the benchmark seed. These assets are CC0,
separately from this repository's MIT license; the original [license
notice](resource/image/kenney-animal-pack/License.txt) is included.
