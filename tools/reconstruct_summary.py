#!/usr/bin/env python3
"""
Reconstructs summary.json from a results suite directory.
Useful if run_all.py failed or was interrupted before writing summary.json.

Usage:
  python3 tools/reconstruct_summary.py [results_suite_dir]
"""

import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any, Dict, List

def _summarize(values: List[float]) -> Dict[str, float]:
    if not values:
        return {}
    if len(values) == 1:
        return {"mean": values[0], "stdev": 0.0}
    return {"mean": float(statistics.fmean(values)), "stdev": float(statistics.stdev(values))}

def main():
    parser = argparse.ArgumentParser(description="Reconstruct summary.json from existing results.")
    parser.add_argument("suite_dir", help="Path to the results suite directory")
    args = parser.parse_args()

    suite_dir = Path(args.suite_dir).resolve()
    if not suite_dir.exists():
        print(f"Error: Directory not found: {suite_dir}", file=sys.stderr)
        return 1

    variants_data = []

    # Structure: output_dir / benchmark / engine / backend / scene
    # We iterate 4 levels deep
    for bench_dir in suite_dir.iterdir():
        if not bench_dir.is_dir(): continue
        
        for engine_dir in bench_dir.iterdir():
            if not engine_dir.is_dir(): continue
            
            for backend_dir in engine_dir.iterdir():
                if not backend_dir.is_dir(): continue
                
                for scene_dir in backend_dir.iterdir():
                    if not scene_dir.is_dir(): continue
                    
                    # Found a variant directory
                    benchmark = bench_dir.name
                    engine = engine_dir.name
                    backend = backend_dir.name
                    scene = scene_dir.name
                    
                    variant_info = {
                        "benchmark": benchmark,
                        "engine": engine,
                        "backend": backend,
                        "scene": scene,
                        "binary": "unknown", # Can't easily reconstruct, but not strictly needed for report
                        "outputs": [],
                        "summary": {}
                    }
                    
                    metrics: Dict[str, List[float]] = {k: [] for k in ["avg_ms", "median_ms", "p95_ms", "p99_ms", "fps"]}
                    
                    # Find run files
                    for run_file in scene_dir.glob("*.json"):
                        if run_file.name == "summary.json": continue
                        
                        try:
                            with run_file.open("r", encoding="utf-8") as f:
                                data = json.load(f)
                        except (json.JSONDecodeError, OSError) as e:
                            print(f"Warning: Failed to read {run_file}: {e}", file=sys.stderr)
                            continue
                            
                        stats = data.get("stats", {})
                        if not stats: continue
                        
                        variant_info["outputs"].append(str(run_file))
                        
                        for key in metrics.keys():
                            val = stats.get(key)
                            if isinstance(val, (int, float)):
                                metrics[key].append(float(val))
                                
                    if not variant_info["outputs"]:
                        continue

                    variant_info["summary"] = {k: _summarize(v) for k, v in metrics.items() if v}
                    variants_data.append(variant_info)

    suite_summary = {
        "runs_per_variant": "unknown",
        "fixed_seed": "unknown",
        "seed_base": "unknown",
        "seeds": [],
        "variants": variants_data,
    }

    summary_path = suite_dir / "summary.json"
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump(suite_summary, f, indent=2)
    
    print(f"Reconstructed summary written to: {summary_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
