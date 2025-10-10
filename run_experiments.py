import os
import subprocess
import json
import itertools
from pathlib import Path
import platform # NEW: Import platform to detect the OS

# --- Configuration ---
# NEW: OS-aware configuration to download the correct gensort binary
system = platform.system()
if system == "Linux":
    GENSORT_URL = "https://www.ordinal.com/try.cgi/gensort-linux-1.5.tar.gz"
    GENSORT_ARCHIVE = Path("gensort-linux-1.5.tar.gz")
    GENSORT_DIR = Path("gensort-linux-1.5")
    GENSORT_EXE = GENSORT_DIR / "64" / "gensort"
elif system == "Darwin": # Darwin is the system name for macOS
    GENSORT_URL = "https://www.ordinal.com/try.cgi/gensort-mac-1.5.tar.gz"
    GENSORT_ARCHIVE = Path("gensort-mac-1.5.tar.gz")
    GENSORT_DIR = Path("gensort-mac-1.5")
    GENSORT_EXE = GENSORT_DIR / "64" / "gensort"
else:
    raise Exception(f"Unsupported OS: {system}")

DATA_DIR = Path("data")
RESULTS_FILE = Path("results.json")

# Adjust these parameters for your experiments
DATA_SIZES = [10**6, 5 * 10**6]
THREAD_COUNTS = [1, 2, 4, 8]
ALGORITHMS = ["radix_sort", "introsort"]
CUTOFFS = [500, 5000, 50000]

# --- Helper Functions ---
def run_command(cmd, cwd="."):
    """Runs a command and prints its output."""
    print(f"Executing: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=str(cwd), check=True)

def setup_environment():
    """Create directories and build dependencies."""
    DATA_DIR.mkdir(exist_ok=True)
    
    if not (Path.cwd() / "sorter").exists():
        print("--- Building C++ sorter using Makefile ---")
        run_command(["make"])

    if not GENSORT_EXE.exists():
        print(f"--- Setting up gensort for {system} ---")
        if not GENSORT_ARCHIVE.exists():
            # CHANGED: Use the URL variable determined by the OS detection
            run_command(["curl", GENSORT_URL, "-o", str(GENSORT_ARCHIVE)])
        
        run_command(["tar", "-xvf", str(GENSORT_ARCHIVE)])
    
def generate_data_if_needed(size):
    """Generates test data using gensort."""
    num_records = size
    output_file = DATA_DIR / f"data_{size}.bin"
    if not output_file.exists():
        print(f"--- Generating data for N={size} ---")
        # CORRECTED: This version of gensort produces 100-byte binary records by default.
        # The -a flag (for ASCII) is removed to match what our C++ code expects.
        run_command([str(GENSORT_EXE), str(num_records), str(output_file)])
    return output_file
    
# --- Main Execution ---
def main():
    setup_environment()
    all_results = []
    
    for size in DATA_SIZES:
        generate_data_if_needed(size)
        
    print("\n--- Starting Experiments ---")
    
    for size, algo, threads in itertools.product(DATA_SIZES, ALGORITHMS, THREAD_COUNTS):
        data_file = DATA_DIR / f"data_{size}.bin"
        
        if algo == "introsort":
            for cutoff in CUTOFFS:
                cmd = ["./sorter", algo, str(threads), str(data_file), str(cutoff)]
                output = subprocess.check_output(cmd).decode("utf-8")
                all_results.append(json.loads(output))
        else: # Radix sort
            cmd = ["./sorter", algo, str(threads), str(data_file)]
            output = subprocess.check_output(cmd).decode("utf-8")
            all_results.append(json.loads(output))

    with open(RESULTS_FILE, "w") as f:
        json.dump(all_results, f, indent=2)
        
    print(f"\n--- Experiments Complete. Results saved to {RESULTS_FILE} ---")

if __name__ == "__main__":
    main()