#!/bin/bash

# This script will exit immediately if any command fails.
set -e

echo "--- Starting the Full Project Workflow ---"

# Step 1: Run the experiment script.
# This script will automatically compile the C++ code, generate data,
# and run all the sorting experiments.
echo "==> Step 1 of 2: Compiling C++ code and running experiments..."
python3 run_experiments.py
echo "==> Experiments finished successfully. Results are in results.json."

# Step 2: Run the report building script.
# This script reads results.json and generates the plots.
echo "==> Step 2 of 2: Building report and generating plots..."
python3 build_report.py
echo "==> Report plots created in the 'report/' directory."

echo "--- Workflow Complete! ---"