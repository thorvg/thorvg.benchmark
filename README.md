# ThorVG Benchmark

<p align="center">
  <img width="550" height="auto" src="https://github.com/thorvg/thorvg.site/blob/main/readme/logo/animated_brand.svg">
</p>

A benchmark comparing **Skia** and **ThorVG** rendering performance.

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
./vcpkg/vcpkg install skia sdl2 --triplet arm64-osx

# thorvg (checkout to a specific tag for testing)
git clone https://github.com/thorvg/thorvg.git
./thorvg/meson setup build -Dloaders=all -Dengines=all
./thorvg/ninja -C build install
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
./build/rectbench_skia_sdl
./build/rectbench_thorvg_sdl
```

## Benchmark CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--backend=cpu\|gl\|webgpu` | `cpu` | Rendering backend |
| `--scene=default\|rotation` | `default` | Scene mode (rotation adds per-frame rotation) |
| `--seed=INT` | `12345` | RNG seed for deterministic output |
| `--frames=INT` | `1000` | Number of measured frames |
| `--warmup=INT` | `120` | Number of warmup frames |
| `--width=INT` | `2560` | Window/render width |
| `--height=INT` | `1440` | Window/render height |
| `--vsync=0\|1` | `0` | Enable VSync |
| `--output=PATH` | auto | Output JSON file path |

### Examples

```bash
# Quick test with fewer frames
./build/rectbench_skia_sdl --frames=100 --warmup=10

# Test with specific backend
./build/rectbench_thorvg_sdl --backend=gl

# Rotation scene mode
./build/rectbench_thorvg_sdl --scene=rotation

# Verify deterministic output (checksums should match)
./build/rectbench_skia_sdl --backend=cpu --seed=42 --frames=1
./build/rectbench_thorvg_sdl --backend=cpu --seed=42 --frames=1
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
| `--benchmarks=...` | all | Comma-separated: `rect,circle,stroke,image,lineargradient,radialgradient` |
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
  ./build/rectbench_skia_sdl --backend=cpu --frames=200 --warmup=20

# Random seeds per run (measure variance)
python3 tools/repeat_seed.py --runs 5 -- \
  ./build/rectbench_thorvg_sdl --backend=gl --frames=200
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

**Output:** Prints a Markdown table comparing FPS across engines and backends:

```
| Benchmark (Scene) | Skia CPU | Skia GL | ThorVG CPU | ThorVG GL | ThorVG WebGPU |
|-------------------|----------|---------|------------|-----------|---------------|
| rect (default)    | 850.23   | 1200.45 | 720.10     | 980.33    | 1100.50       |
| circle (rotation) | 640.12   | 890.67  | 580.44     | 750.22    | 820.15        |
```

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
  libsdl2-dev libgl1-mesa-dev libegl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libxfixes-dev

# vcpkg + Skia
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install skia --triplet x64-linux

# thorvg (checkout to a specific tag for testing)
git clone https://github.com/thorvg/thorvg.git
./thorvg/meson setup build -Dloaders=all -Dengines=all
./thorvg/ninja -C build install
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

## Notes

- **Skia**: Installed via vcpkg
- **ThorVG**: Built from the `thorvg` [repo](https://github.com/thorvg/thorvg)
- **wgpu-native**: See the [guide](https://github.com/thorvg/thorvg/wiki/WebGPU-Engine-Development)