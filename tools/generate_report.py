#!/usr/bin/env python3
"""
Reads a summary.json from tools/run_all.py and outputs a Markdown table.

Usage:
  python3 tools/generate_report.py [path/to/summary.json]
"""

import argparse
import json
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Generate Markdown table from benchmark summary.")
    parser.add_argument("summary_json", nargs="?", help="Path to summary.json file")
    args = parser.parse_args()

    # Try to find summary.json if not provided
    if not args.summary_json:
        # Default to most recent results_suite directory if possible, or just error
        # For simplicity, let's require the argument or look in current dir
        if Path("summary.json").exists():
            json_path = Path("summary.json")
        else:
            print("Error: Please provide path to summary.json", file=sys.stderr)
            return 1
    else:
        json_path = Path(args.summary_json)

    if not json_path.exists():
        print(f"Error: File not found: {json_path}", file=sys.stderr)
        return 1

    try:
        with json_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError as e:
        print(f"Error: Failed to parse JSON: {e}", file=sys.stderr)
        return 1

    variants = data.get("variants", [])
    if not variants:
        print("No variants found in summary.", file=sys.stderr)
        return 0

    # Define the columns we want
    # Label, Engine, Backend
    columns = [
        ("Skia CPU", "skia", "cpu"),
        ("Skia GL", "skia", "gl"),
        ("ThorVG CPU", "thorvg", "cpu"),
        ("ThorVG GL", "thorvg", "gl"),
        ("ThorVG WebGPU", "thorvg", "webgpu"),
    ]

    # Organize data: dict[(bench, scene)] -> dict[(engine, backend)] -> fps
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
        fps_stats = summary.get("fps", {})
        mean_fps = fps_stats.get("mean", 0.0)
        
        rows[row_key][(engine, backend)] = mean_fps

    # Print Header
    header = "| Benchmark (Scene) | " + " | ".join(c[0] for c in columns) + " |"
    print(header)
    print("|---" + "|---" * len(columns) + "|")

    # Sort rows by benchmark then scene
    sorted_keys = sorted(rows.keys())

    for bench, scene in sorted_keys:
        row_str = f"| {bench} ({scene}) |"
        
        for unused_label, eng, back in columns:
            val = rows[(bench, scene)].get((eng, back))
            if val is not None:
                row_str += f" {val:.2f} |"
            else:
                row_str += " - |"
        
        print(row_str)

    return 0

if __name__ == "__main__":
    sys.exit(main())
