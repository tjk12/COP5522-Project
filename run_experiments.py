import os
import subprocess
import json
import itertools
from pathlib import Path
import platform
import sys

# --- Configuration ---
system = platform.system()
if system == "Linux":
    GENSORT_DIR = Path.cwd()
    GENSORT_EXE = GENSORT_DIR / "64" / "gensort"
elif system == "Darwin": # macOS
    GENSORT_DIR = Path("gensort-mac-1.5")
    GENSORT_EXE = GENSORT_DIR / "64" / "gensort"
else:
    raise Exception(f"Unsupported OS: {system}")

# Use the system's scratch space for large data files if SCRATCH env var is set
scratch_dir = Path(os.getenv('SCRATCH', Path.cwd()))
DATA_DIR = scratch_dir / "sorting_project_data"

RESULTS_FILE = Path("results.json")

# Define the experiment parameters
DATA_SIZES = [50 * 10**6]
THREAD_COUNTS = [1, 2, 4, 8, 16, 32, 64, 128]
ALGORITHMS = ["scalable_radix_sort", "hybrid_merge_sort"]

# --- Helper Functions ---
def run_command(cmd, cwd="."):
    """Runs a shell command and prints its output."""
    print(f"Executing: {' '.join(cmd)}")
    # CHANGED: Replaced capture_output and text with older, Python 3.6 compatible arguments
    result = subprocess.run(cmd, cwd=str(cwd), 
                            stdout=subprocess.PIPE, 
                            stderr=subprocess.PIPE,
                            universal_newlines=True, 
                            check=True)
    if result.stdout:
        print(f"STDOUT:\n{result.stdout}")
    if result.stderr:
        print(f"STDERR:\n{result.stderr}")
    return result

def setup_environment():
    """Create data directories and build the C++ sorter executable."""
    DATA_DIR.mkdir(exist_ok=True) 
    
    sorter_path = Path.cwd() / "sorter"
    main_cpp_path = Path.cwd() / "main.cpp"

    if not sorter_path.exists() or (main_cpp_path.exists() and main_cpp_path.stat().st_mtime > sorter_path.stat().st_mtime):
        print("--- Building C++ sorter using Makefile ---")
        run_command(["make"])
    else:
        print("--- Sorter executable is up-to-date. Skipping build. ---")

    if not GENSORT_EXE.exists():
        print(f"--- ERROR: gensort executable not found at '{GENSORT_EXE}' ---")
        print("Please download and unpack gensort into a 'gensort-linux-1.5' or 'gensort-mac-1.5' directory in your project root.")
        sys.exit(1)
    
def generate_data_if_needed(size):
    """Generates test data using gensort for a given size."""
    num_records = size
    output_file = DATA_DIR / f"data_{size}.bin"
    if not output_file.exists():
        print(f"--- Generating data for N={size} records (this may take a while) ---")
        run_command([str(GENSORT_EXE), str(num_records), str(output_file)])
    else:
        print(f"--- Data file for N={size} already exists. Skipping generation. ---")
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
        
        cmd = ["./sorter", algo, str(threads), str(data_file)]
        
        try:
            # For the main experiment runs, we just need the stdout for JSON
            output = subprocess.check_output(cmd).decode("utf-8")
            result_json = json.loads(output)
            all_results.append(result_json)
            print(f"Ran {algo} N={size} T={threads}: Time={result_json['time_ms']:.2f}ms, Correct={result_json['correct']}")
        except subprocess.CalledProcessError as e:
            print(f"Error running command: {' '.join(cmd)}")
            print(f"STDOUT:\n{e.stdout}")
            print(f"STDERR:\n{e.stderr}")
            sys.exit(1)

    with open(RESULTS_FILE, "w") as f:
        json.dump(all_results, f, indent=2)
        
    print(f"\n--- Experiments Complete. Results saved to {RESULTS_FILE} ---")

if __name__ == "__main__":
    main()