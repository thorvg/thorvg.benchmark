#!/usr/bin/env python3
"""
Run the benchmark suite (multiple benchmarks/engines/backends/scenes) in one script.

Examples:
  # Run all known benchmarks, defaults
  python3 tools/run_all.py --runs 3

  # Run only specific benchmarks
  python3 tools/run_all.py --benchmarks=rect,circle --runs 3

  # Specify build directory for binaries
  python3 tools/run_all.py --bin-dir=./build --runs 3
"""

from __future__ import annotations

import argparse
import json
import secrets
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


@dataclass(frozen=True)
class Variant:
    benchmark: str
    engine: str
    binary: str
    backend: str
    scene: str

    @property
    def name(self) -> str:
        return f"{self.benchmark}_{self.engine}_{self.backend}_{self.scene}"


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _summarize(values: list[float]) -> dict[str, float]:
    if not values:
        return {}
    if len(values) == 1:
        return {"mean": values[0], "stdev": 0.0}
    return {"mean": float(statistics.fmean(values)), "stdev": float(statistics.stdev(values))}


def _csv(s: str) -> list[str]:
    return [p.strip() for p in s.split(",") if p.strip()]


def _expand_all(values: list[str], all_values: list[str]) -> list[str]:
    lowered = [v.lower() for v in values]
    if "all" in lowered:
        return list(all_values)
    return lowered


def _strip_conflicting_kv_args(args: list[str], *, keys: set[str]) -> tuple[list[str], list[str]]:
    """
    Remove any '--key=value' args in `keys` from `args`.

    We always control `--seed` and `--output` per-run; allowing the caller to
    pass them via extra args is a common footgun that can lead to missing/empty
    output files.
    """
    kept: list[str] = []
    stripped: list[str] = []
    prefixes = tuple(f"--{k}=" for k in keys)
    for a in args:
        if a.startswith(prefixes):
            stripped.append(a)
        else:
            kept.append(a)
    return kept, stripped


def _default_output_dir() -> str:
    return time.strftime("results_suite_%Y%m%d_%H%M%S")


def _splitmix64(x: int) -> int:
    # Deterministic 64-bit generator (stable across Python versions).
    x = (x + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = x
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9 & 0xFFFFFFFFFFFFFFFF
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB & 0xFFFFFFFFFFFFFFFF
    return (z ^ (z >> 31)) & 0xFFFFFFFFFFFFFFFF


# Default seed base matches C++ default (12345)
DEFAULT_SEED_BASE = 12345


def _pick_seeds(runs: int, fixed_seed: int | None, seed_base: int | None) -> list[int]:
    """Generate seeds for each run.
    
    - fixed_seed: All runs use the same seed (for measuring variance with identical input)
    - seed_base: Each run gets a different seed derived from base (default behavior)
    
    The same seeds are used across all engines/backends/scenes within a run,
    ensuring fair comparison.
    """
    if fixed_seed is not None:
        return [fixed_seed for _ in range(runs)]
    
    # Use seed_base (default: 12345) to generate deterministic per-run seeds
    base = int(seed_base if seed_base is not None else DEFAULT_SEED_BASE) & 0xFFFFFFFFFFFFFFFF
    return [_splitmix64(base + i) for i in range(runs)]


def _variants(
    bin_dir: Path,
    benchmarks: Iterable[str],
    engines: Iterable[str],
    backends: Iterable[str],
    scenes: Iterable[str],
) -> list[Variant]:
    supported_backends = {
        "skia": {"cpu", "gl"},
        "thorvg": {"cpu", "gl", "webgpu"},
    }
    known_backends: set[str] = set().union(*supported_backends.values())

    backends = [b.lower() for b in backends]
    for backend in backends:
        if backend not in known_backends:
            raise ValueError(f"unknown backend: {backend}")

    out: list[Variant] = []
    for bench in benchmarks:
        bench = bench.lower()
        for engine in engines:
            engine = engine.lower()
            if engine not in supported_backends:
                raise ValueError(f"unknown engine: {engine}")

            # Construct binary path: {bench}_{engine}_sdl
            # Valid names e.g. rectbench_skia_sdl, circlebench_thorvg_sdl
            # So if bench is "rect", binary is "rectbench_skia_sdl"
            # But the user might pass "rectbench" or "rect". Let's normalize.
            # Actually, per existing names: rectbench_skia_sdl
            # So if bench="rect", we want "rectbench".
            # Let's assume the user passes "rect", "circle" etc.
            
            # Helper to map short name -> full bench prefix if needed,
            # or just assume bench + "bench" if it doesn't have it.
            # Existing specific names:
            #   rectbench_skia_sdl
            #   circlebench_skia_sdl
            #   strokebench_skia_sdl
            #   imagebench_skia_sdl
            #   lineargradientbench_skia_sdl
            #   radialgradientbench_skia_sdl
            
            # Logic: if 'bench' is suffix, leave it, else append 'bench'.
            bench_prefix = bench if bench.endswith("bench") else f"{bench}bench"
            
            bin_name = f"{bench_prefix}_{engine}_sdl"
            binary_path = bin_dir / bin_name

            for backend in backends:
                if backend not in supported_backends[engine]:
                    continue
                for scene in scenes:
                    scene = scene.lower()
                    if scene not in {"default", "rotation"}:
                        raise ValueError(f"unknown scene: {scene}")
                    
                    # Convert absolute path to string
                    out.append(Variant(
                        benchmark=bench,
                        engine=engine,
                        binary=str(binary_path.resolve()),
                        backend=backend,
                        scene=scene
                    ))
    return out


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run a benchmark suite (multiple benchmarks/engines/backends/scenes).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build binaries with CMake before running",
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="CMake build directory (used with --build and for finding binaries)",
    )
    parser.add_argument(
        "--build-jobs",
        type=int,
        default=8,
        help="Parallel jobs for CMake build (used with --build)",
    )
    parser.add_argument("--runs", type=int, default=3, help="Repeats per variant")
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Fixed seed for ALL runs (same input each run, for measuring variance)",
    )
    parser.add_argument(
        "--seed-base",
        type=int,
        default=None,
        help=f"Base for per-run seeds (default: {DEFAULT_SEED_BASE}). "
             "Each run gets a different seed derived from this base. "
             "Same seed used across all engines/backends within each run.",
    )
    parser.add_argument(
        "--benchmarks",
        default="rect,circle,stroke,image,lineargradient,radialgradient",
        help="Comma-separated benchmarks to run (or 'all')",
    )
    parser.add_argument(
        "--engines",
        default="skia,thorvg",
        help="Comma-separated engines to run (or 'all')",
    )
    parser.add_argument(
        "--backends",
        default="cpu,gl",
        help="Comma-separated backends to run (or 'all'; thorvg also supports webgpu)",
    )
    parser.add_argument(
        "--scenes",
        default="default,rotation",
        help="Comma-separated scenes to run (or 'all')",
    )
    parser.add_argument("--bin-dir", default=None, help="Directory containing binaries (defaults to --build-dir)")
    parser.add_argument("--output-dir", default=None, help="Directory to write all outputs")
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=None,
        help="Per-run wall-clock timeout; kills the run if exceeded",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print commands but do not execute")
    parser.add_argument(
        "extra_args",
        nargs=argparse.REMAINDER,
        help="Extra args passed to every benchmark (prefix with --)",
    )
    args = parser.parse_args(argv)

    if args.runs <= 0:
        parser.error("--runs must be >= 1")
    if args.build_jobs <= 0:
        parser.error("--build-jobs must be >= 1")
    if args.seed is not None and args.seed_base is not None:
        parser.error("use only one of --seed or --seed-base")

    extra_args = list(args.extra_args)
    if extra_args and extra_args[0] == "--":
        extra_args = extra_args[1:]

    extra_args, stripped = _strip_conflicting_kv_args(extra_args, keys={"seed", "output"})
    if stripped:
        print(
            "Note: ignoring extra args that conflict with per-run settings: "
            + " ".join(stripped),
            file=sys.stderr,
        )

    if args.build:
        build_cmd = ["cmake", "--build", args.build_dir, "-j", str(args.build_jobs)]
        print(f"Building: {' '.join(build_cmd)}", flush=True)
        if not args.dry_run:
            proc = subprocess.run(build_cmd)
            if proc.returncode != 0:
                return proc.returncode

    output_dir = Path(args.output_dir or _default_output_dir())
    if not args.dry_run:
        output_dir.mkdir(parents=True, exist_ok=True)

    # Use absolute paths in benchmark invocations so the output location does
    # not depend on the current working directory of the subprocess.
    output_dir = output_dir.resolve()

    benchmarks_list = _csv(args.benchmarks)
    engines = _csv(args.engines)
    backends = _csv(args.backends)
    scenes = _csv(args.scenes)

    # We don't have a rigid list of "all" benchmarks here, but user can say "all" if we define it.
    # For now, let's treat "all" as the default list.
    ALL_BENCHS = ["rect", "circle", "stroke", "image", "lineargradient", "radialgradient"]
    benchmarks_list = _expand_all(benchmarks_list, ALL_BENCHS)
    engines = _expand_all(engines, ["skia", "thorvg"])
    backends = _expand_all(backends, ["cpu", "gl", "webgpu"])
    scenes = _expand_all(scenes, ["default", "rotation"])

    bin_src = Path(args.bin_dir if args.bin_dir else args.build_dir)

    try:
        variants = _variants(
            bin_src,
            benchmarks_list,
            engines,
            backends,
            scenes,
        )
    except ValueError as e:
        parser.error(str(e))
    if not variants:
        print("No variants selected (check --benchmarks/--engines/--backends/--scenes).", file=sys.stderr)
        return 2

    if not args.dry_run:
        missing_bins = [v.binary for v in variants if not Path(v.binary).exists()]
        if missing_bins:
            print("Missing benchmark binaries:", file=sys.stderr)
            for b in sorted(set(missing_bins)):
                print(f"  {b}", file=sys.stderr)
            return 2

    seeds = _pick_seeds(
        args.runs,
        None if args.seed is None else int(args.seed),
        None if args.seed_base is None else int(args.seed_base),
    )

    suite_summary: dict[str, Any] = {
        "runs_per_variant": args.runs,
        "fixed_seed": args.seed,
        "seed_base": args.seed_base,
        "seeds": seeds,
        "variants": [],
    }

    for variant in variants:
        # Structure: output_dir / benchmark / engine / backend / scene
        variant_dir = output_dir / variant.benchmark / variant.engine / variant.backend / variant.scene
        if not args.dry_run:
            variant_dir.mkdir(parents=True, exist_ok=True)

        metrics: dict[str, list[float]] = {k: [] for k in ["avg_ms", "median_ms", "p95_ms", "p99_ms", "fps"]}
        outputs: list[str] = []

        print(f"\n== {variant.name} ==")
        for idx, seed in enumerate(seeds, start=1):
            out_json = (variant_dir / f"{variant.name}_seed{seed}_run{idx:02d}.json").resolve()
            out_log = variant_dir / f"{variant.name}_seed{seed}_run{idx:02d}.log"

            cmd = [
                variant.binary,
                f"--backend={variant.backend}",
                f"--scene={variant.scene}",
                f"--seed={seed}",
                f"--output={out_json}",
                *extra_args,
            ]
            print(f"[{idx}/{args.runs}] {' '.join(cmd)}")

            if args.dry_run:
                continue

            try:
                proc = subprocess.run(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=args.timeout_seconds,
                )
            except subprocess.TimeoutExpired as e:
                out_log.write_text((e.stdout or "") + "\n[TIMEOUT]\n", encoding="utf-8")
                print(f"timed out after {args.timeout_seconds}s; log: {out_log}", file=sys.stderr)
                return 124

            out_log.write_text(proc.stdout, encoding="utf-8")
            if proc.returncode != 0:
                print(f"failed (exit {proc.returncode}); log: {out_log}", file=sys.stderr)
                return proc.returncode

            if not out_json.exists():
                print(f"missing output JSON: {out_json}; log: {out_log}", file=sys.stderr)
                return 2
            if out_json.stat().st_size == 0:
                print(f"empty output JSON: {out_json}; log: {out_log}", file=sys.stderr)
                return 2

            outputs.append(str(out_json))
            try:
                data = _load_json(out_json)
            except json.JSONDecodeError as e:
                print(f"invalid JSON in output: {out_json} ({e}); log: {out_log}", file=sys.stderr)
                return 2
            stats = data.get("stats", {})
            for key in list(metrics.keys()):
                val = stats.get(key)
                if isinstance(val, (int, float)):
                    metrics[key].append(float(val))

        suite_summary["variants"].append(
            {
                "benchmark": variant.benchmark,
                "engine": variant.engine,
                "backend": variant.backend,
                "scene": variant.scene,
                "binary": variant.binary,
                "outputs": outputs,
                "summary": {k: _summarize(v) for k, v in metrics.items() if v},
            }
        )

    if not args.dry_run:
        summary_path = output_dir / "summary.json"
        summary_path.write_text(json.dumps(suite_summary, indent=2) + "\n", encoding="utf-8")
        print(f"\nWrote suite summary: {summary_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
