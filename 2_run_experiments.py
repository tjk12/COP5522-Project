#!/usr/bin/env python3
"""
Experiment runner for parallel sorting benchmarks.
Assumes test data has been generated separately using generate_data.py.
Executables are located in subdirectories (1_bseq_proj, 2_oseq_proj, etc.)

Sample manual commands (Linux) to run each sorter executable from their
subdirectories. These are useful for quick manual testing.
1) Basic sequential sorter (always single-threaded):
   ./basic_seq_sorter merge_sort ../sorting_project_data/data_50000000.bin
2) Optimized sequential sorter (single-threaded optimized):
   ./optimized_seq_sorter merge_sort ../sorting_project_data/data_50000000.bin
3) Basic OpenMP parallel sorter (set OMP_NUM_THREADS as needed):
   export OMP_NUM_THREADS=8
   ./basic_omp_sorter merge_sort ../sorting_project_data/data_50000000.bin
4) Optimized MPI+OpenMP hybrid sorter (use mpirun/mpiexec):
   # Example: 4 MPI processes, 2 threads per process
   mpirun -np 4 ./optimized_mpi_sorter merge_sort 2 ../sorting_project_data/data_50000000.bin
"""
import os
import subprocess
import json
import shutil
from pathlib import Path
import platform
import sys
import argparse

# --- Configuration ---
system = platform.system()

# Use the system's scratch space for large data files if PROJECT env var
scratch_dir = Path(os.getenv('PROJECT', Path.cwd()))
DATA_DIR = scratch_dir / "sorting_project_data"

RESULTS_FILE = Path("results.json")
PROJECT_ROOT = Path.cwd()

# Experiment Definitions
DEFAULT_SIZES = [
    6_250_000,
    12_500_000,
    25_000_000,
    50_000_000,
    100_000_000,
    200_000_000,
    400_000_000
]

# Max size for sequential experiments to avoid excessive runtime
MAX_SEQUENTIAL_SIZE = 100_000_000

# Allow user to supply sizes via command-line or interactively in the console.
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument('--sizes', help='Comma-separated list of record counts (e.g. 10000000,50000000)')
parser.add_argument('-y', '--yes', action='store_true', help='Accept defaults without prompting')
parser.add_argument('--no-mpi', action='store_true', help='Disable MPI experiments')
args, _unknown = parser.parse_known_args()

if args.sizes:
    try:
        DATA_SIZES = [int(s.strip()) for s in args.sizes.split(',') if s.strip()]
        if not DATA_SIZES:
            raise ValueError()
    except ValueError:
        print(f"Invalid --sizes value: '{args.sizes}'. Expected comma-separated integers.")
        sys.exit(1)
else:
    # Interactive prompt if running in a TTY and user didn't pass -y
    if sys.stdin.isatty() and not args.yes:
        print(f"Default data sizes: {DEFAULT_SIZES}")
        resp = input("Press Enter to accept, or enter comma-separated sizes: ").strip()
        if resp == "":
            DATA_SIZES = DEFAULT_SIZES
        else:
            try:
                DATA_SIZES = [int(s.strip()) for s in resp.split(',') if s.strip()]
                if not DATA_SIZES:
                    raise ValueError()
            except ValueError:
                print("Invalid input. Expected comma-separated integers. Exiting.")
                sys.exit(1)
    else:
        DATA_SIZES = DEFAULT_SIZES

# Subdirectory locations for each executable
EXECUTABLE_PATHS = {
    "basic_seq_sorter": PROJECT_ROOT / "1_bseq_proj" / "basic_seq_sorter",
    "optimized_seq_sorter": (PROJECT_ROOT / "2_oseq_proj" /
                             "optimized_seq_sorter"),
    "basic_omp_sorter": PROJECT_ROOT / "3_bpar_proj" / "basic_omp_sorter",
    "optimized_mpi_sorter": (PROJECT_ROOT / "4_opar_proj" /
                             "optimized_mpi_sorter")
}


EXPERIMENTS = {
    "sequential": {
        "enabled": True,
        "executables": {
            "basic_seq_sorter": ["merge_sort", "radix_sort"],
            "optimized_seq_sorter": [
                "merge_sort",
                "radix_sort"
            ],
        },
        "threads": [1], # Sequential is always single-threaded
    },
    "openmp": {
        "enabled": True,
        "executables": {
            "basic_omp_sorter": [
                "merge_sort",
                "radix_sort"
            ],
        },
        "threads": [1, 4, 16, 64] # Bridges has 128 cores per node, Adjust as needed
    },
    "mpi": {
        "enabled": not args.no_mpi,
        "executables": {
            "optimized_mpi_sorter": [
                "merge_sort",
                "radix_sort"
            ],
        },
        "processes": [1, 2, 3, 4, 5], # Use 5 nodes max
        "threads_per_process": [64] # Bridges has 128 cores per node, Adjust as needed
    }
}


def exe_fullpath(exe_name):
    """
    Return the full path to an executable on disk,
    taking Windows .exe extension into account.
    """
    base_path = EXECUTABLE_PATHS.get(exe_name)
    if base_path is None:
        return None
    
    if system == 'Windows':
        candidate = base_path.with_suffix('.exe')
    else:
        candidate = base_path
    
    return str(candidate)


def check_data_exists():
    """Verify that generated data files exist."""
    if not DATA_DIR.exists():
        print(f"ERROR: Data directory not found: {DATA_DIR}")
        print("Please run generate_data.py first to create test data.")
        sys.exit(1)
    
    missing_sizes = []
    for size in DATA_SIZES:
        data_file = DATA_DIR / f"data_{size}.bin"
        if not data_file.exists():
            missing_sizes.append(size)
    
    if missing_sizes:
        print(f"ERROR: Missing data files for sizes: {missing_sizes}")
        print("Please run generate_data.py first to create test data.")
        sys.exit(1)
    
    print(f"✓ Data directory verified: {DATA_DIR}")


def build_missing_executables():
    """
    Build any missing executables from their subdirectories.
    Each subdirectory has its own Makefile.
    """
    print("\n--- Checking for missing executables ---")
    
    build_dirs = {
        "basic_seq_sorter": PROJECT_ROOT / "1_bseq_proj",
        "optimized_seq_sorter": PROJECT_ROOT / "2_oseq_proj",
        "basic_omp_sorter": PROJECT_ROOT / "3_bpar_proj",
        "optimized_mpi_sorter": PROJECT_ROOT / "4_opar_proj"
    }
    
    missing = []
    for exe_name, build_dir in build_dirs.items():
        exe_path = exe_fullpath(exe_name)
        if exe_path and not Path(exe_path).exists():
            missing.append((exe_name, build_dir))
        else:
            status = "✓" if exe_path and Path(exe_path).exists() else "✗"
            print(f"{status} {exe_name}")
    
    if not missing:
        print("All available executables are up-to-date.")
        return
    
    # Check for MPI
    have_mpicxx = (shutil.which('mpic++') is not None or
                   shutil.which('mpicc') is not None)
    
    if not have_mpicxx:
        missing = [(name, path) for name, path in missing 
                   if name != "optimized_mpi_sorter"]
        print("Note: MPI compiler not found. Skipping MPI build.")
    
    if missing:
        print(f"\nBuilding {len(missing)} missing executable(s)...")
        for exe_name, build_dir in missing:
            print(f"\nBuilding {exe_name} from {build_dir}...")
            try:
                result = subprocess.run(
                    ["make"],
                    cwd=str(build_dir),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True
                )
                if result.returncode == 0:
                    print(f"✓ Successfully built {exe_name}")
                else:
                    print(f"✗ Build failed for {exe_name}")
                    print(f"STDERR: {result.stderr}")
            except Exception as e:
                print(f"✗ Error building {exe_name}: {e}")


def load_existing_results():
    if RESULTS_FILE.exists():
        try:
            with open(RESULTS_FILE, 'r') as f:
                return json.load(f)
        except json.JSONDecodeError:
            print(f"Warning: Could not decode {RESULTS_FILE}. Starting fresh.")
            return []
    return []


def is_done(results, query):
    if not query:
        return False
    for res in results:
        match = True
        for k, v in query.items():
            # Compare as strings to handle potential type mismatches
            if str(res.get(k)) != str(v):
                match = False
                break
        if match:
            return True
    return False


def run_and_collect(cmd, results_list, env=None, check_params=None):
    """
    Executes a command, parses JSON output, appends to results_list.
    env may be provided (a dict) to modify env vars such as
    OMP_NUM_THREADS.
    """
    if check_params and is_done(results_list, check_params):
        print(f"  Skipping (already done): {check_params}")
        return

    print(f"  Running: {' '.join(cmd)}")
    try:
        output = subprocess.check_output(
            cmd,
            stderr=subprocess.PIPE,
            env=env,
            universal_newlines=True
        )
        output_str = output.strip()
        try:
            result_json = json.loads(output_str)
        except json.JSONDecodeError:
            for line in output_str.splitlines():
                line = line.strip()
                if line.startswith('{') and line.endswith('}'):
                    result_json = json.loads(line)
                    break
            else:
                print("ERROR: Could not decode JSON from command's output.")
                print(f"STDOUT:\n{output}")
                return

        results_list.append(result_json)
        if 'mpi_procs' in result_json:
            print(f"MPI Procs: {result_json.get('mpi_procs')}, "
                  f"OMP Threads: "
                  f"{result_json.get('omp_threads', 1)}, "
                  f"Time: {result_json.get('time_ms'):.2f}ms, "
                  f"Correct: {result_json.get('correct')}")
        else:
            print(f"    Threads: {result_json.get('threads')}, "
                  f"Time: {result_json.get('time_ms'):.2f}ms, "
                  f"Correct: {result_json.get('correct')}")

        # Save incrementally
        with open(RESULTS_FILE, "w") as f:
            json.dump(results_list, f, indent=2)

    except subprocess.CalledProcessError as e:
        print(f"ERROR running command: {' '.join(cmd)}")
        try:
            stderr = e.stderr
        except Exception:
            stderr = str(e)
        print(f"    STDERR: {stderr}")


def main():
    print("=== Sorting Benchmark Experiment Runner ===\n")
    
    # Check data files exist
    check_data_exists()
    
    # Build missing executables
    build_missing_executables()
    
    all_results = load_existing_results()
    if all_results:
        print(f"Loaded {len(all_results)} existing results from {RESULTS_FILE}")

    print("\n=== Starting Experiments ===")

    # Sequential
    if EXPERIMENTS['sequential']['enabled']:
        print("\n--- Sequential Experiments ---")
        for exe in (
                EXPERIMENTS['sequential']['executables'].keys()):
            exe_path = exe_fullpath(exe)
            if not exe_path or not Path(exe_path).exists():
                print(f"Skipping {exe} (not found: {exe_path})")
                continue
            
            algos = (
                EXPERIMENTS['sequential']['executables'][exe])
            for size in DATA_SIZES:
                if size > MAX_SEQUENTIAL_SIZE:
                    print(f"Skipping sequential sort for N={size} (exceeds limit)")
                    continue
                data_file = DATA_DIR / f"data_{size}.bin"
                for algo in algos:
                    cmd = [exe_path, algo, str(data_file)]
                    run_and_collect(cmd, all_results,
                                    check_params={'algorithm': algo, 'N': size})

    # OpenMP
    if EXPERIMENTS['openmp']['enabled']:
        print("\n--- OpenMP Experiments ---")
        for exe in EXPERIMENTS['openmp']['executables'].keys():
            exe_path = exe_fullpath(exe)
            if not exe_path or not Path(exe_path).exists():
                print(f"Skipping {exe} (not found: {exe_path})")
                continue
            
            algos = EXPERIMENTS['openmp']['executables'][exe]
            for size in DATA_SIZES:
                data_file = DATA_DIR / f"data_{size}.bin"
                for algo in algos:
                    for threads in (
                            EXPERIMENTS['openmp']['threads']):
                        env = os.environ.copy()
                        env['OMP_NUM_THREADS'] = str(threads)
                        cmd = [exe_path, algo, str(data_file)]
                        cmd = [exe_path, algo, str(data_file)]
                        run_and_collect(cmd, all_results,
                                        env=env,
                                        check_params={'algorithm': algo, 'N': size, 'threads': threads})

    # MPI / Hybrid (optional)
    if EXPERIMENTS['mpi']['enabled']:
        print("\n--- MPI/Hybrid Experiments ---")
        mpirun_cmd = (shutil.which('mpirun') or
                      shutil.which('mpiexec'))
        if not mpirun_cmd:
            print("mpirun/mpiexec not found; "
                  "skipping MPI experiments.")
        else:
            for exe in (
                    EXPERIMENTS['mpi']['executables'].keys()):
                exe_path = exe_fullpath(exe)
                if not exe_path or not Path(exe_path).exists():
                    print(f"Skipping {exe} "
                          f"(not found: {exe_path})")
                    continue
                
                algos = EXPERIMENTS['mpi']['executables'][exe]
                for size in DATA_SIZES:
                    data_file = DATA_DIR / f"data_{size}.bin"
                    for algo in algos:
                        for procs in (
                                EXPERIMENTS['mpi'][
                                    'processes']):
                            if exe == 'optimized_mpi_sorter':
                                for threads in (
                                    EXPERIMENTS['mpi'][
                                        'threads_per_process']):
                                    cmd = [mpirun_cmd, '-np',
                                           str(procs),
                                           exe_path, algo,
                                           str(threads),
                                           str(data_file)]
                                    cmd = [mpirun_cmd, '-np',
                                           str(procs),
                                           exe_path, algo,
                                           str(threads),
                                           str(data_file)]
                                    run_and_collect(cmd,
                                                    all_results,
                                                    check_params={'algorithm': algo, 'N': size, 'mpi_procs': procs, 'omp_threads': threads})
                            else:
                                cmd = [mpirun_cmd, '-np',
                                       str(procs), exe_path,
                                       algo, str(data_file)]
                                cmd = [mpirun_cmd, '-np',
                                       str(procs), exe_path,
                                       algo, str(data_file)]
                                run_and_collect(cmd,
                                                all_results,
                                                check_params={'algorithm': algo, 'N': size, 'mpi_procs': procs})

    save_results(all_results)


def save_results(all_results):
    """Save results to JSON file."""
    with open(RESULTS_FILE, "w") as f:
        json.dump(all_results, f, indent=2)
    print("\n=== Experiments Complete ===")
    print(f"Results saved to: {RESULTS_FILE}")


if __name__ == "__main__":
    main()
