#!/bin/bash

# --- SLURM Configuration ---
#SBATCH -N 5                     # Max nodes to use (for the 5-node test)
#SBATCH -t 04:00:00              # Max walltime (4 hours, adjust as needed)
#SBATCH -p RM                    # Partition (RM is common, check Bridges docs)

# Exit immediately if a command exits with a non-zero status.
set -e

echo "=========================================="
echo "   Full Project Workflow"
echo "=========================================="

echo "Loading modules..."
# Load modules for Bridges (Intel compiler and Intel MPI are common)
module purge
# Switch to OpenMPI and GCC stack
module load openmpi/5.0.8-gcc13.3.1

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

echo ""
echo "=========================================="
echo "   Workflow Complete!"
echo "=========================================="
echo ""
