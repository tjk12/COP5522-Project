#!/usr/bin/env python3
"""
Standalone data generation script for sorting experiments.
Generates test data using gensort for a given size.
Run this once before running experiments.
"""

import os
import subprocess
import sys
from pathlib import Path
import platform

# --- Configuration ---
system = platform.system()
if system == "Linux":
    GENSORT_EXE = Path.cwd() / "gensort_libraries" / "64" / "gensort"
elif system == "Darwin":  # macOS
    GENSORT_EXE = Path.cwd() / "gensort_libraries" / "64" / "gensort"
elif system == "Windows":
    GENSORT_EXE = Path.cwd() / "gensort_libraries" / "win" / "gensort.exe"
else:
    raise Exception(f"Unsupported OS: {system}")

# Use the system's scratch space for large data files if PROJECT env var is set
scratch_dir = Path(os.getenv('PROJECT', Path.cwd()))
DATA_DIR = scratch_dir / "sorting_project_data"

# Default data sizes to generate
DATA_SIZES = [50 * 10**6]


def run_command(cmd):
    """Runs a shell command and prints its output."""
    print(f"Executing: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True,
                                check=True)
        if result.stdout:
            print(f"STDOUT:\n{result.stdout}")
        if result.stderr:
            print(f"STDERR:\n{result.stderr}")
        return result
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {' '.join(cmd)}")
        if e.stdout:
            print(f"STDOUT:\n{e.stdout}")
        if e.stderr:
            print(f"STDERR:\n{e.stderr}")
        raise


def generate_data(sizes=None):
    """Generates test data using gensort for given sizes."""
    if sizes is None:
        sizes = DATA_SIZES
    
    # Check if gensort exists
    if not Path(GENSORT_EXE).exists():
        print(f"ERROR: gensort not found at '{GENSORT_EXE}'")
        print("Please ensure gensort is available in "
              "the gensort_libraries directory.")
        sys.exit(1)

    # Create data directory
    DATA_DIR.mkdir(exist_ok=True)
    print(f"Data directory: {DATA_DIR}")

    # Generate data for each size
    for size in sizes:
        output_file = DATA_DIR / f"data_{size}.bin"
        if output_file.exists():
            print(f"Data file for N={size} already exists: {output_file}")
            print("Skipping generation (remove file to regenerate)")
        else:
            print(f"\nGenerating data for N={size} records...")
            num_records = size
            run_command([str(GENSORT_EXE), str(num_records), str(output_file)])
            print(f"Successfully generated: {output_file}")

    print("\n--- Data generation complete ---")
    print(f"Data files are available in: {DATA_DIR}")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        try:
            custom_sizes = [int(arg) for arg in sys.argv[1:]]
            print(f"Generating data for custom sizes: {custom_sizes}")
            generate_data(custom_sizes)
        except ValueError:
            print("Error: All arguments must be integers (number of records)")
            sys.exit(1)
    else:
        generate_data()
