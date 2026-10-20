[![License](https://img.shields.io/badge/licence-MIT-green.svg?style=flat)](LICENSE)
[![Wikipedia](https://img.shields.io/badge/Wikipedia-000000?style=flat&logo=wikipedia&logoColor=white)](https://en.wikipedia.org/wiki/Thor_Vector_Graphics)
[![Discord](https://img.shields.io/badge/Community-5865f2?style=flat&logo=discord&logoColor=white)](https://discord.gg/n25xj6J6HM)
[![OpenCollective](https://img.shields.io/badge/OpenCollective-84B5FC?style=flat&logo=opencollective&logoColor=white)](https://opencollective.com/thorvg)

# ThorVG Benchmark

<p align="center">
  <img width="550" height="auto" src="https://github.com/thorvg/thorvg.site/blob/main/readme/logo/animated_brand.svg">
</p>

A benchmark comparing **Skia** and **ThorVG** rendering performance.

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
against recording API substitutes. They check both engines' submission/wait/
presentation order and option forwarding in all 16 factories. Source-line
mapping attributes coverage to those method bodies; it does not measure the
substituted engine APIs, graphics initialization, or hardware behavior. GL
constructor initializers are also executed to check synchronization flag binding;
their graphics setup bodies are excluded from CRAP scores.
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

## Quick Start (macOS)

### 1. Clone & Initialize

```bash
git clone <this-repo>
cd thorvg.benchmark
```

### 2. Install Dependencies

```bash
# System dependencies
brew install cmake ninja meson wgpu-native libomp python3

# vcpkg (one-time setup)
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install skia sdl3 --triplet arm64-osx

# thorvg (optionally checkout to a specific tag for testing)
git clone https://github.com/thorvg/thorvg.git
meson setup thorvg/build thorvg -Dloaders=all -Dengines=all
ninja -C thorvg/build install
```

### 3. Build

```bash
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH" \
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build build -j
```

### 4. Run

```bash
./build/rect_skia_sdl
./build/rect_tvg_sdl
./build/multiimage_skia_sdl
./build/multiimage_tvg_sdl
```

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

## Benchmark CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--backend=cpu\|gl\|webgpu` | `cpu` | Rendering backend |
| `--scene=default\|rotation` | `default` | Scene mode (rotation adds per-frame rotation) |
| `--seed=INT` | `12345` | RNG seed for deterministic output |
| `--frames=INT` | `1000` | Number of measured frames (at least 1) |
| `--warmup=INT` | `120` | Number of warmup frames |
| `--width=INT` | `2560` | Window/render width |
| `--height=INT` | `1440` | Window/render height |
| `--vsync=0\|1` | `0` | Enable VSync |
| `--gpu_sync=0\|1` | `0` | Both GL engines: wait for GPU completion before presentation |
| `--opacity=FLOAT` | `1` | Strokebench only: stroke opacity from 0 to 1 |
| `--output=PATH` | auto | Output JSON file path |

Use the same `--gpu_sync` value for both engines in a GL comparison. With `0`,
neither adapter requests an explicit GPU completion wait; with `1`, both call
`glFinish()` after submission. The frame timer includes update, drawing, the
configured wait, and presentation. Setup and event processing are excluded,
and warmup frames never enter the reported statistics.

### Examples

```bash
# Quick test with fewer frames
./build/rect_skia_sdl --frames=100 --warmup=10

# Test with specific backend
./build/rect_tvg_sdl --backend=gl

# Rotation scene mode
./build/rect_tvg_sdl --scene=rotation

# Verify deterministic output (checksums should match)
./build/rect_skia_sdl --backend=cpu --seed=42 --frames=1
./build/rect_tvg_sdl --backend=cpu --seed=42 --frames=1
```

## Tools

### `run_all.py` – Run Benchmark Suite

Run a matrix of benchmarks across multiple engines, backends, and scenes:

```bash
# Basic usage
python3 tools/run_all.py --runs 3 -- --frames=200 --warmup=20

# Build before running
python3 tools/run_all.py --build --runs 3 -- --frames=200 --warmup=20

# Specific benchmarks only
python3 tools/run_all.py --benchmarks=rect,circle --runs 3

# CPU backend only
python3 tools/run_all.py --backends=cpu --runs 5

# Fixed seed for reproducibility
python3 tools/run_all.py --seed=42 --runs 3
```

**Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `--runs N` | `3` | Number of runs per variant |
| `--benchmarks=...` | all | Comma-separated: `rect,circle,stroke,image,multiimage,lineargradient,radialgradient` |
| `--engines=...` | `skia,thorvg` | Engines to test |
| `--backends=...` | `cpu,gl` | Backends: `cpu`, `gl`, `webgpu` (ThorVG only) |
| `--scenes=...` | `default,rotation` | Scene modes |
| `--build` | off | Build with CMake before running |
| `--seed N` | - | Fixed seed for ALL runs (use for measuring variance with identical input) |
| `--seed-base N` | `12345` | Base for per-run seeds. Each run uses a different seed derived from this base. Same seed across all engines/backends within each run for fair comparison. |
| `--output-dir PATH` | auto | Results directory (default: `results_suite_<timestamp>`) |
| `--dry-run` | off | Print commands without executing |

**Seed behavior:** By default, run 1 uses seed derived from `12345+0`, run 2 from `12345+1`, etc. This ensures:
- Different random input each run
- Same input for all engines/backends within a run (fair comparison)

**Output:** Results are saved to `results_suite_<timestamp>/` with:
- Per-run JSON files organized by benchmark/engine/backend/scene
- `summary.json` with aggregated statistics

---

### `repeat_seed.py` – Repeat with Fixed Seed

Run the same benchmark multiple times with identical or varied seeds:

```bash
# Fixed seed (all runs identical)
python3 tools/repeat_seed.py --runs 5 --seed 42 -- \
  ./build/rect_skia_sdl --backend=cpu --frames=200 --warmup=20

# Random seeds per run (measure variance)
python3 tools/repeat_seed.py --runs 5 -- \
  ./build/rect_tvg_sdl --backend=gl --frames=200
```

**Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `--runs N` | `5` | Number of runs |
| `--seed N` | random | Fixed seed (omit for different seed per run) |
| `--output-dir PATH` | `results_repeat` | Output directory |
| `--timeout-seconds N` | none | Kill run if exceeded |
| `--dry-run` | off | Print commands without executing |

---

### `generate_report.py` – Generate Markdown Report

Create a comparison table from benchmark results:

```bash
python3 tools/generate_report.py results_suite_*/summary.json
```

**Output:** Prints a Markdown table comparing FPS across engines and backends.

## Output Format

Benchmark results are written as JSON:

```json
{
  "stats": {
    "avg_ms": 1.23,
    "median_ms": 1.20,
    "p95_ms": 1.50,
    "p99_ms": 1.80,
    "min_ms": 1.00,
    "max_ms": 2.00,
    "stddev_ms": 0.15,
    "fps": 810.37
  },
  "metadata": {
    "engine": "skia",
    "backend": "cpu",
    "seed": 12345,
    "resolution": "2560x1440"
  }
}
```

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
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build -j
```

## Clean Rebuild

```bash
rm -rf build
# Repeat the build step
```

## Image Assets

The [Kenney Animal Pack](resource/image/kenney-animal-pack/README.md) provides
80 2D PNG assets (72 unique file contents) for image benchmarks. The
`multiimage` manifest uses 25 byte-distinct files from this pack and
selects them deterministically with the benchmark seed. These assets are CC0,
separately from this repository's MIT license; the original [license
notice](resource/image/kenney-animal-pack/License.txt) is included.

## Notes

- **Skia**: Installed via vcpkg
- **ThorVG**: Built from the `thorvg` [repo](https://github.com/thorvg/thorvg)
- **wgpu-native**: See the [guide](https://github.com/thorvg/thorvg/wiki/WebGPU-Engine-Development)
