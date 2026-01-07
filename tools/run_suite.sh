#!/bin/bash
set -e

# Generate a default output directory name with timestamp
OUTPUT_DIR="results_suite_$(date +%Y%m%d_%H%M%S)"

echo "Running benchmarks..."
echo "Output directory: $OUTPUT_DIR"

# Run the benchmark suite
# Pass all arguments to run_all.py, and force the output directory
python3 tools/run_all.py --output-dir="$OUTPUT_DIR" "$@"

echo ""
echo "Generating Markdown report..."
echo "========================================================"

# Generate and display the report
python3 tools/generate_report.py "$OUTPUT_DIR/summary.json"

echo "========================================================"
echo "Full results saved in: $OUTPUT_DIR"
