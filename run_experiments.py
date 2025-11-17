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
elif system == "Windows":
    GENSORT_DIR = Path.cwd()
    GENSORT_EXE = GENSORT_DIR / "win" / "gensort.exe"
else:
    raise Exception(f"Unsupported OS: {system}")

# Use the system's scratch space for large data files if SCRATCH env var is set
scratch_dir = Path(os.getenv('SCRATCH', Path.cwd()))
DATA_DIR = scratch_dir / "sorting_project_data"

RESULTS_FILE = Path("results.json")

# --- Experiment Definitions ---
# Define which experiments to run for each implementation type.
# This makes it easy to enable/disable runs.

DATA_SIZES = [50 * 10**6]

EXPERIMENTS = {
    "sequential": {
        "enabled": True,
        "executables": {
            "basic_seq_sorter": ["basic_merge_sort", "basic_radix_sort"],
            "optimized_seq_sorter": ["optimized_merge_sort", "optimized_radix_sort"],
        },
        "threads": [1], # Sequential runs always use 1 thread
    },
    "mpi": {
        "enabled": True, # Set to False to skip MPI runs
        "executables": {
            "basic_mpi_sorter": ["hybrid_merge_sort", "scalable_radix_sort"],
            "optimized_mpi_sorter": ["hybrid_merge_sort", "scalable_radix_sort"],
        },
        "processes": [1, 2, 4], # Number of MPI processes (e.g., nodes)
        "threads_per_process": [1, 2, 4, 8, 16], # For the optimized hybrid version
    }
}

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

    # Check if any C++ file is newer than any target executable
    cpp_files = list(Path.cwd().glob('*.cpp'))
    targets = [Path(t) for t in EXPERIMENTS["openmp"].values()]

    # Simplified check: just build every time. A more robust check would be complex.
    print("--- Building all C++ sorters using Makefile ---")
    run_command(["make", "all"])
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
    
    # Generate all required data files first
    for size in DATA_SIZES:
        generate_data_if_needed(size)
        
    print("\n--- Starting Experiments ---")

    # --- Run Sequential Experiments ---
    if EXPERIMENTS["sequential"]["enabled"]:
        print("\n--- Running Sequential Experiments ---")
        for exe, algos in EXPERIMENTS["sequential"]["executables"].items():
            for size in DATA_SIZES:
                for algo in algos:
                    data_file = DATA_DIR / f"data_{size}.bin"
                    cmd = [f"./{exe}", algo, str(data_file)]
                    run_and_collect(cmd, all_results)

    # --- Run MPI Experiments ---
    if EXPERIMENTS["mpi"]["enabled"]:
        print("\n--- Running MPI Experiments ---")
        for exe, algos in EXPERIMENTS["mpi"]["executables"].items():
            for size in DATA_SIZES:
                for algo in algos:
                    for procs in EXPERIMENTS["mpi"]["processes"]:
                        data_file = DATA_DIR / f"data_{size}.bin"
                        if exe == "basic_mpi_sorter":
                            # Basic MPI version doesn't use OpenMP threads
                            cmd = ["mpirun", "-np", str(procs), f"./{exe}", algo, str(data_file)]
                            run_and_collect(cmd, all_results)
                        elif exe == "optimized_mpi_sorter":
                            # Optimized version is hybrid, test with different thread counts
                            for threads in EXPERIMENTS["mpi"]["threads_per_process"]:
                                cmd = ["mpirun", "-np", str(procs), f"./{exe}", algo, str(threads), str(data_file)]
                                run_and_collect(cmd, all_results)

    # --- Save Results ---
    save_results(all_results)

def run_and_collect(cmd, results_list):
    """Executes a command, parses the JSON output, and appends it to a list."""
    print(f"Running: {' '.join(cmd)}")
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.PIPE).decode("utf-8")
        result_json = json.loads(output)
        results_list.append(result_json)
        
        # Provide more detailed output for MPI runs
        if "mpi_procs" in result_json:
            print(f"  - MPI Procs: {result_json['mpi_procs']}, OMP Threads: {result_json.get('omp_threads', 1)}, "
                  f"Total Threads: {result_json['threads']}, Time: {result_json['time_ms']:.2f}ms, "
                  f"Correct: {result_json['correct']}")
        else:
            print(f"  - Threads: {result_json['threads']}, Time: {result_json['time_ms']:.2f}ms, "
                  f"Correct: {result_json['correct']}")

    except subprocess.CalledProcessError as e:
        print(f"  - ERROR running command: {' '.join(cmd)}")
        print(f"  - STDERR:\n{e.stderr.decode('utf-8')}")
    except json.JSONDecodeError:
        print(f"  - ERROR: Could not decode JSON from the command's output.")
        print(f"  - STDOUT:\n{output}")

def save_results(all_results):
    """Saves the collected results to a JSON file."""
    with open(RESULTS_FILE, "w") as f:
        json.dump(all_results, f, indent=2)
        
    print(f"\n--- Experiments Complete. Results saved to {RESULTS_FILE} ---")

if __name__ == "__main__":
    main()