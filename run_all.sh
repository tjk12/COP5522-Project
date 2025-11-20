#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

echo "=========================================="
echo "   Full Project Workflow"
echo "=========================================="

# Step 1: Generate data if not present
echo ""
echo "==> Step 1 of 3: Generating test data (if needed)..."
python3 1_generate_data.py

echo "    ✓ Test data ready"

# Step 2: Run experiments
echo ""
echo "==> Step 2 of 3: Running experiments..."
python3 2_run_experiments.py

echo "    ✓ Experiments finished successfully"
echo "    Results saved to: results.json"

# Step 3: Build report and generate plots
echo ""
echo "==> Step 3 of 3: Building report and generating plots..."
python3 3_build_report.py

echo "    ✓ Report plots created"
echo "    Report directory: report/"

echo ""
echo "=========================================="
echo "   Workflow Complete!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  - Review results.json for raw experiment data"
echo "  - Check report/ directory for visualization plots:"
echo "    • scalability.png - Runtime vs. Threads"
echo "    • speedup.png - Parallel Speedup"
echo "    • throughput.png - Algorithm Throughput"