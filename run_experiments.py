import os
import subprocess
import json
import shutil
from pathlib import Path
import platform
import sys

# --- Configuration ---
system = platform.system()
if system == "Linux":
    GENSORT_EXE = Path.cwd() / "64" / "gensort"
elif system == "Darwin":  # macOS
    GENSORT_EXE = Path.cwd() / "64" / "gensort"
elif system == "Windows":
    GENSORT_EXE = Path.cwd() / "win" / "gensort.exe"
else:
    raise Exception(f"Unsupported OS: {system}")

# Use the system's scratch space for large data files if PROJECT env var is set
scratch_dir = Path(os.getenv('PROJECT', Path.cwd()))
DATA_DIR = scratch_dir / "sorting_project_data"

RESULTS_FILE = Path("results.json")

# --- Experiment Definitions ---
DATA_SIZES = [50 * 10**6]

# Executable names (Makefile targets produce these executables)
EXECUTABLES = {
    "basic_seq_sorter": "basic_seq_sorter",  # basic_seq_proj
    "optimized_seq_sorter": "optimized_seq_sorter",  # optimized_seq_proj
    "basic_omp_sorter": "basic_omp_sorter",  # basic_parallel_proj
    "optimized_mpi_sorter": "optimized_mpi_sorter"  # optimized_parallel_proj
}

EXPERIMENTS = {
    "sequential": {
        "enabled": True,
        "executables": {
            "basic_seq_sorter": ["basic_merge_sort", "basic_radix_sort"],
            "optimized_seq_sorter": ["optimized_merge_sort", "optimized_radix_sort"],
        },
        "threads": [1],
    },
    "openmp": {
        "enabled": True,
        "executables": {
            "basic_omp_sorter": ["hybrid_merge_sort", "scalable_radix_sort"],
        },
        "threads": [1, 2, 4, 8]
    },
    "mpi": {
        "enabled": True,
        "executables": {
            "optimized_mpi_sorter": ["hybrid_merge_sort", "scalable_radix_sort"],
        },
        "processes": [1, 2, 3, 4, 5],
        "threads_per_process": [1, 2, 4, 8]
    }
}


def run_command(cmd, cwd="."):
    """Runs a shell command and prints its output."""
    print(f"Executing: {' '.join(cmd)}")
    try:
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
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {' '.join(cmd)}")
        if e.stdout:
            print(f"STDOUT:\n{e.stdout}")
        if e.stderr:
            print(f"STDERR:\n{e.stderr}")
        raise


def exe_fullpath(exe_name):
    """
    Return the full path to an executable on disk,
    taking Windows .exe into account.
    """
    if system == 'Windows':
        candidate = Path.cwd() / (exe_name + '.exe')
    else:
        candidate = Path.cwd() / exe_name
    return str(candidate)


def setup_environment():
    """
    Create data directory and build necessary executables using Makefile.
    Builds non-MPI targets if MPI toolchain is missing.
    """
    DATA_DIR.mkdir(exist_ok=True)

    have_mpicxx = (shutil.which('mpic++') is not None or
                   shutil.which('mpicc') is not None)
    make_targets = [
        EXECUTABLES['basic_seq_sorter'],
        EXECUTABLES['optimized_seq_sorter'],
        EXECUTABLES['basic_omp_sorter']
    ]
    if have_mpicxx:
        make_targets.append(EXECUTABLES['optimized_mpi_sorter'])
    else:
        print("Note: MPI compiler not found. Skipping MPI/hybrid target build")

    missing = []
    for t in make_targets:
        path = exe_fullpath(t)
        if not Path(path).exists():
            missing.append(t)

    if missing:
        print("--- Building required C++ sorters using Makefile ---")
        try:
            run_command(["make"] + missing)
        except Exception:
            print("Build failed. Try running `make` to inspect errors")
    else:
        print("--- Sorter executables are up-to-date. Skipping build. ---")

    if not Path(GENSORT_EXE).exists():
        print(f"--- ERROR: gensort not found at '{GENSORT_EXE}' ---")
        print("Please download and unpack gensort into the appropriate folder"
              "under the project root (see README).")
        sys.exit(1)


def generate_data_if_needed(size):
    """Generates test data using gensort for a given size."""
    num_records = size
    output_file = DATA_DIR / f"data_{size}.bin"
    if not output_file.exists():
        print(f"--- Generating data for N={size} records ---")
        run_command([str(GENSORT_EXE), str(num_records), str(output_file)])
    else:
        print(f"--- Data file for N={size} already exists. Skipping generation ---")
    return output_file


def run_and_collect(cmd, results_list, env=None):
    """
    Executes a command, parses the JSON output, and appends it to results_list.
    env may be provided (a dict) to modify env vars such as OMP_NUM_THREADS.
    """
    print(f"Running: {' '.join(cmd)}")
    try:
        output = subprocess.check_output(cmd, stderr=subprocess.PIPE, env=env).decode('utf-8')
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
                  f"OMP Threads: {result_json.get('omp_threads', 1)}, "
                  f"Threads: {result_json.get('threads')}, "
                  f"Time: {result_json.get('time_ms'):.2f}ms, "
                  f"Correct: {result_json.get('correct')}")
        else:
            print(f"Threads: {result_json.get('threads')}, "
                  f"Time: {result_json.get('time_ms'):.2f}ms, "
                  f"Correct: {result_json.get('correct')}")

    except subprocess.CalledProcessError as e:
        print(f"  - ERROR running command: {' '.join(cmd)}")
        try:
            stderr = e.stderr.decode('utf-8')
        except Exception:
            stderr = str(e)
        print(f"  - STDERR:\n{stderr}")


def main():
    setup_environment()
    all_results = []

    for size in DATA_SIZES:
        generate_data_if_needed(size)

    print("\n--- Starting Experiments ---")

    # Sequential
    if EXPERIMENTS['sequential']['enabled']:
        print("\n--- Running Sequential Experiments ---")
        for exe, algos in EXPERIMENTS['sequential']['executables'].items():
            exe_path = exe_fullpath(exe)
            if not Path(exe_path).exists():
                print(f"  - Skipping {exe} (executable not found: {exe_path})")
                continue
            for size in DATA_SIZES:
                data_file = DATA_DIR / f"data_{size}.bin"
                for algo in algos:
                    cmd = [exe_path, algo, str(data_file)]
                    run_and_collect(cmd, all_results)

    # OpenMP
    if EXPERIMENTS['openmp']['enabled']:
        print("\n--- Running OpenMP Experiments ---")
        for exe, algos in EXPERIMENTS['openmp']['executables'].items():
            exe_path = exe_fullpath(exe)
            if not Path(exe_path).exists():
                print(f"  - Skipping {exe} (executable not found: {exe_path})")
                continue
            for size in DATA_SIZES:
                data_file = DATA_DIR / f"data_{size}.bin"
                for algo in algos:
                    for threads in EXPERIMENTS['openmp']['threads']:
                        env = os.environ.copy()
                        env['OMP_NUM_THREADS'] = str(threads)
                        cmd = [exe_path, algo, str(data_file)]
                        run_and_collect(cmd, all_results, env=env)

    # MPI / Hybrid (optional)
    if EXPERIMENTS['mpi']['enabled']:
        print("\n--- Running MPI/Hybrid Experiments ---")
        mpirun_cmd = shutil.which('mpirun') or shutil.which('mpiexec')
        if not mpirun_cmd:
            print("mpirun/mpiexec not found; skipping MPI experiments.")
        else:
            for exe, algos in EXPERIMENTS['mpi']['executables'].items():
                exe_path = exe_fullpath(exe)
                if not Path(exe_path).exists():
                    print(f"Skipping {exe} (executable not found: {exe_path})")
                    continue
                for size in DATA_SIZES:
                    data_file = DATA_DIR / f"data_{size}.bin"
                    for algo in algos:
                        for procs in EXPERIMENTS['mpi']['processes']:
                            if exe == 'optimized_mpi_sorter':
                                for threads in EXPERIMENTS['mpi']['threads_per_process']:
                                    cmd = [mpirun_cmd, '-np', str(procs), exe_path, algo, str(threads), str(data_file)]
                                    run_and_collect(cmd, all_results)
                            else:
                                cmd = [mpirun_cmd, '-np', str(procs), exe_path, algo, str(data_file)]
                                run_and_collect(cmd, all_results)

    save_results(all_results)


def save_results(all_results):
    with open(RESULTS_FILE, "w") as f:
        json.dump(all_results, f, indent=2)
    print(f"\n--- Experiments Complete. Results saved to {RESULTS_FILE} ---")


if __name__ == "__main__":
    main()