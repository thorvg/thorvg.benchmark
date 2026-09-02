[![License](https://img.shields.io/badge/licence-MIT-green.svg?style=flat)](LICENSE)
[![Wikipedia](https://img.shields.io/badge/Wikipedia-000000?style=flat&logo=wikipedia&logoColor=white)](https://en.wikipedia.org/wiki/Thor_Vector_Graphics)
[![Discord](https://img.shields.io/badge/Community-5865f2?style=flat&logo=discord&logoColor=white)](https://discord.gg/n25xj6J6HM)
[![OpenCollective](https://img.shields.io/badge/OpenCollective-84B5FC?style=flat&logo=opencollective&logoColor=white)](https://opencollective.com/thorvg)

# ThorVG Benchmark

<p align="center">
  <img width="550" height="auto" src="https://github.com/thorvg/thorvg.site/blob/main/readme/logo/animated_brand.svg">
</p>

This repository compares the same primitive microbenchmarks across Skia,
ThorVG, NanoVG, Pathfinder, and Vello. It measures the repository's seven
existing workloads rather than the engines' complete feature sets.

| Engine | Backend | Scene model | Dependency |
|---|---|---|---|
| Skia | CPU, OpenGL | immediate | configured vcpkg package |
| ThorVG | CPU, OpenGL, WebGPU | retained | installed package or latest `main` source build |
| NanoVG | OpenGL 3.3 core | immediate | configured vcpkg package |
| Pathfinder | OpenGL GL3/D3D9 | immediate | crates.io `0.5` packages |
| Vello | WebGPU/wgpu | immediate | crates.io `0.10.0` package |

The default suite contains `rect`, `circle`, `stroke`, `image`,
`lineargradient`, `radialgradient`, and `strokerect`, each in `default` and
`rotation` scene modes. SVG, arbitrary-path, text, clipping, and animation
workloads are intentionally out of scope.

## Timing and comparability

Every publishable GPU sample measures:

```text
update -> encode/render/submit -> present -> explicit GPU completion
```

OpenGL completion uses `glFinish()` after the swap. WebGPU completion waits for
the submitted queue work after presentation. `--gpu_sync=1` is the default;
`--gpu_sync=0` is diagnostic-only and is never ranked.

OpenGL adapters request a 3.3 core context, RGBA8, depth24, stencil8, double
buffering, and no MSAA. They query the actual swap interval. WebGPU adapters
prefer Immediate; Mailbox and FIFO results are marked `vsync-limited`. Only
schema-v2 runs with an exact drawable size, verified non-limiting presentation,
complete raw samples, explicit GPU completion, and a Release build are
comparable. ThorVG uses a fixed four-thread configuration.
Image results record a runtime SHA-256 of the exact asset path selected by the
adapter; the suite rejects it if it differs from the manifest's canonical hash.
Within each workload, scene, and API, the suite ranks GPU results only when
every repeat and engine reports the same GPU device and pixel format. A change
in either keeps the measurements for diagnostics but excludes the group from
performance conclusions.

Adapters use a black background, source-over blending, antialiasing, butt
caps with miter joins/miter limit 4, bilinear image sampling, clamped/padded
gradients, and the same center-based scale/rotate/translate order. The ThorVG
image adapter relies on ThorVG's fixed linear sampler because its public
`Picture` API has no per-picture filtering switch.

## Build

Requirements are CMake 3.20+, Ninja, SDL2, and the SDK for the selected engine.
Rust engines require Rust 1.88 or newer. Skia and NanoVG use vcpkg packages;
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
| `--gpu_sync=0\|1` | `1` | explicit completion wait |
| `--capture=PATH` | none | lossless correctness capture |
| `--output=PATH` | generated | schema-v2 JSON path |

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
