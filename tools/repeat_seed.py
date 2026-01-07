#!/usr/bin/env python3
"""
Repeat a rectbench binary multiple times with the same RNG seed.

Example:
  python3 tools/repeat_seed.py --runs 5 --seed 42 -- \
    ./build/rectbench_skia_sdl --backend=cpu --frames=200 --warmup=20
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import secrets
from pathlib import Path
from typing import Any


def _safe_stem(s: str) -> str:
    keep = []
    for ch in s:
        if ch.isalnum() or ch in ("-", "_", "."):
            keep.append(ch)
        else:
            keep.append("_")
    out = "".join(keep).strip("_")
    return out or "cmd"


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _summarize(values: list[float]) -> str:
    if not values:
        return "n/a"
    if len(values) == 1:
        return f"{values[0]:.6f}"
    mean = statistics.fmean(values)
    stdev = statistics.stdev(values)
    return f"{mean:.6f} ± {stdev:.6f}"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Repeat a rectbench run multiple times with a fixed seed.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--runs", type=int, default=5, help="Number of runs")
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="RNG seed to force (omit to generate a different seed per run)",
    )
    parser.add_argument(
        "--output-dir",
        default="results_repeat",
        help="Directory for per-run JSON + logs",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print commands but do not execute",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=None,
        help="Per-run wall-clock timeout; kills the run if exceeded",
    )
    parser.add_argument(
        "cmd",
        nargs=argparse.REMAINDER,
        help="Command to run (prefix with -- before the command)",
    )

    args = parser.parse_args(argv)

    cmd = list(args.cmd)
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        parser.error("missing command; pass it after '--', e.g. -- ./build/rectbench_skia_sdl")

    runs = args.runs
    if runs <= 0:
        parser.error("--runs must be >= 1")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    cmd_stem = _safe_stem(Path(cmd[0]).name)
    fixed_seed = None if args.seed is None else int(args.seed)

    # Ensure our --seed/--output flags take precedence even if the caller
    # included them. We accept only '--key=value' style in the C++ benchmarks,
    # but strip both '--key=value' and '--key value' forms here to avoid
    # confusing failures.
    base_cmd: list[str] = []
    stripped: list[str] = []
    i = 0
    while i < len(cmd):
        a = cmd[i]
        if a.startswith("--seed=") or a.startswith("--output="):
            stripped.append(a)
            i += 1
            continue
        if a in {"--seed", "--output"}:
            stripped.append(a)
            if i + 1 < len(cmd):
                stripped.append(cmd[i + 1])
                i += 2
            else:
                i += 1
            continue
        base_cmd.append(a)
        i += 1

    if stripped:
        print(
            "Note: ignoring command args that conflict with per-run settings: "
            + " ".join(stripped),
            file=sys.stderr,
        )

    outputs: list[Path] = []
    stats_by_key: dict[str, list[float]] = {
        "avg_ms": [],
        "median_ms": [],
        "p95_ms": [],
        "p99_ms": [],
        "fps": [],
    }

    used_seeds: set[int] = set()
    for i in range(1, runs + 1):
        if fixed_seed is None:
            while True:
                run_seed = secrets.randbits(64)
                if run_seed not in used_seeds:
                    used_seeds.add(run_seed)
                    break
        else:
            run_seed = fixed_seed

        out_json = output_dir / f"{cmd_stem}_seed{run_seed}_run{i:02d}.json"
        out_log = output_dir / f"{cmd_stem}_seed{run_seed}_run{i:02d}.log"

        run_cmd = [*base_cmd, f"--seed={run_seed}", f"--output={out_json}"]
        print(f"[{i}/{runs}] seed={run_seed} {' '.join(run_cmd)}")

        if args.dry_run:
            continue

        try:
            proc = subprocess.run(
                run_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=args.timeout_seconds,
            )
        except subprocess.TimeoutExpired as e:
            out_log.write_text((e.stdout or "") + "\n[TIMEOUT]\n", encoding="utf-8")
            print(f"[{i}/{runs}] timed out after {args.timeout_seconds}s; log: {out_log}", file=sys.stderr)
            return 124

        out_log.write_text(proc.stdout, encoding="utf-8")
        if proc.returncode != 0:
            print(f"[{i}/{runs}] failed (exit {proc.returncode}); log: {out_log}", file=sys.stderr)
            return proc.returncode

        if not out_json.exists():
            print(f"[{i}/{runs}] missing output JSON: {out_json}; log: {out_log}", file=sys.stderr)
            return 2
        if out_json.stat().st_size == 0:
            print(f"[{i}/{runs}] empty output JSON: {out_json}; log: {out_log}", file=sys.stderr)
            return 2

        outputs.append(out_json)
        try:
            data = _load_json(out_json)
        except json.JSONDecodeError as e:
            print(f"[{i}/{runs}] invalid JSON: {out_json} ({e}); log: {out_log}", file=sys.stderr)
            return 2
        stats = data.get("stats", {})
        for key in list(stats_by_key.keys()):
            val = stats.get(key)
            if isinstance(val, (int, float)):
                stats_by_key[key].append(float(val))

    if args.dry_run:
        return 0

    print("\nSummary (mean ± stdev over runs):")
    for key, values in stats_by_key.items():
        if values:
            print(f"  {key}: {_summarize(values)}")

    print("\nOutputs:")
    for path in outputs:
        print(f"  {path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
