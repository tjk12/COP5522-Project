#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

echo "--- Starting the Full Project Workflow ---"

# Clean up previous runs to ensure fresh data and build
echo "==> Cleaning up previous build and data files..."
make clean
# Explicitly remove generated data to ensure gensort re-runs for a clean slate
# The Python script will re-create this if it doesn't exist.
rm -rf $SCRATCH/sorting_project_data

echo "==> Step 1 of 2: Compiling C++ code and running experiments..."
python3 run_experiments.py

echo "==> Experiments finished successfully. Results are in results.json."

echo "==> Step 2 of 2: Building report and generating plots..."
python3 build_report.py

echo "==> Report plots created in the 'report/' directory."
echo "--- Workflow Complete! ---"