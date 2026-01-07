#!/usr/bin/env python3
import json
import sys
from pathlib import Path

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 tools/compare_stroke.py [path/to/summary.json]")
        sys.exit(1)

    json_path = Path(sys.argv[1])
    with json_path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    variants = data.get("variants", [])
    
    rows = {}
    for v in variants:
        bench = v.get("benchmark", "Unknown")
        scene = v.get("scene", "Unknown")
        engine = v.get("engine", "Unknown")
        backend = v.get("backend", "Unknown")
        
        row_key = (bench, scene)
        if row_key not in rows:
            rows[row_key] = {}
        
        summary = v.get("summary", {})
        fps = summary.get("fps", {}).get("mean", 0.0)
        ms = summary.get("avg_ms", {}).get("mean", 0.0)
        
        rows[row_key][(engine, backend)] = {"fps": fps, "ms": ms}

    # Comparison configurations
    comparisons = [
        ("CPU", "cpu"),
        ("GL", "gl"),
    ]

    print("# Stroke Benchmark Comparison Report")
    print("\n## Raw Performance (FPS)")
    header = "| Benchmark (Scene) | Skia CPU | ThorVG CPU | Ratio (ThorVG/Skia) | Skia GL | ThorVG GL | Ratio (ThorVG/Skia) |"
    sep = "|---|---|---|---|---|---|---|"
    print(header)
    print(sep)

    sorted_keys = sorted(rows.keys())
    for bench, scene in sorted_keys:
        row = rows[(bench, scene)]
        
        def get_data(eng, back):
            return row.get((eng, back), {"fps": 0, "ms": 0})

        skia_cpu = get_data("skia", "cpu")["fps"]
        thorvg_cpu = get_data("thorvg", "cpu")["fps"]
        cpu_ratio = thorvg_cpu / skia_cpu if skia_cpu > 0 else 0

        skia_gl = get_data("skia", "gl")["fps"]
        thorvg_gl = get_data("thorvg", "gl")["fps"]
        gl_ratio = thorvg_gl / skia_gl if skia_gl > 0 else 0

        print(f"| {bench} ({scene}) | {skia_cpu:.2f} | {thorvg_cpu:.2f} | **{cpu_ratio:.2f}x** | {skia_gl:.2f} | {thorvg_gl:.2f} | **{gl_ratio:.2f}x** |")

    print("\n## Execution Time (ms)")
    header = "| Benchmark (Scene) | Skia CPU | ThorVG CPU | Skia GL | ThorVG GL |"
    sep = "|---|---|---|---|---|"
    print(header)
    print(sep)
    for bench, scene in sorted_keys:
        row = rows[(bench, scene)]
        skia_cpu_ms = get_data("skia", "cpu")["ms"]
        thorvg_cpu_ms = get_data("thorvg", "cpu")["ms"]
        skia_gl_ms = get_data("skia", "gl")["ms"]
        thorvg_gl_ms = get_data("thorvg", "gl")["ms"]
        print(f"| {bench} ({scene}) | {skia_cpu_ms:.2f} | {thorvg_cpu_ms:.2f} | {skia_gl_ms:.2f} | {thorvg_gl_ms:.2f} |")

if __name__ == "__main__":
    main()
