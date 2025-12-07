# COP5522-Project
## Overview

The project now has a fully automated 3-step workflow that can be run with:

```bash
./run_all.sh
```

## What run_all.sh Does
### Step 1: Generate Test Data
- **Script**: `1_generate_data.py`
- **Action**: Generates 50M sorting records using the gensort binary
- **Smart**: Only generates data if it doesn't already exist
- **Output**: `sorting_project_data/data_50000000.bin`

### Step 2: Run Experiments
- **Script**: `2_run_experiments.py`
- **Action**: Builds missing executables and runs all sorting experiments
- **Features**:
  - Auto-detects and builds missing executables from subdirectories
  - Runs sequential, OpenMP, and optional MPI experiments
  - Sets `OMP_NUM_THREADS` environment variable for thread count variation (1, 2, 4, 8)
  - Finds test data in the same directory as Step 1
  - Parses JSON output from each experiment
- **Output**: `results.json` (array of experiment results)

### Step 3: Build Analysis Report
- **Script**: `3_build_report.py`
- **Action**: Generates visualization plots from experiment results
- **Features**:
  - Validates `results.json` exists and is readable
  - Checks for required JSON fields before plotting
  - Gracefully skips throughput plot if field not present
  - Calculates speedup metrics (vs. 1 thread baseline)
- **Outputs**:
  - `report/scalability.png` - Runtime vs. Thread Count
  - `report/speedup.png` - Parallel Speedup vs. Ideal
  - `report/throughput.png` - Mega-Keys/Second (if available)

## Data Flow

```
run_all.sh
├── Step 1: 1_generate_data.py
│   └── Creates: sorting_project_data/data_50000000.bin
│
├── Step 2: 2_run_experiments.py
│   ├── Reads: sorting_project_data/data_50000000.bin
│   ├── Reads: executable binaries from 1_bseq_proj/, 2_oseq_proj/, etc.
│   └── Creates: results.json
│
└── Step 3: 3_build_report.py
    ├── Reads: results.json
    └── Creates: report/scalability.png
               report/speedup.png
               report/throughput.png
```
## Usage Examples

### Standard workflow (all 3 steps):
```bash
./run_all.sh
```

### Run individual steps:
```bash
# Generate data only
python3 1_generate_data.py

# Run experiments only (requires data from step 1)
python3 2_run_experiments.py

# Build report only (requires results from step 2)
python3 3_build_report.py
```

### Custom data generation sizes:
```bash
# Generate multiple sizes
python3 1_generate_data.py 10000000 50000000 100000000

python3 2_run_experiments.py --sizes 10000000
```

## Manual C++ build & run (no Python)

If you want to compile and run the C++ sorting programs directly (without any Python scripts), the repository includes a top-level `Makefile` that delegates to each subproject. The following shows how to build, create input with the included `gensort` binaries, and run an example with N = 100000 records.

1) Build the projects

```bash
# From the repository root
make          # builds all subprojects (or use make -j4 for parallel builds)

# Or build a single subproject:
make bseq     # builds 1_bseq_proj/basic_seq_sorter
make oseq     # builds 2_oseq_proj/optimized_seq_sorter
make bpar     # builds 3_bpar_proj/basic_omp_sorter
make opar     # builds 4_opar_proj/optimized_mpi_sorter (or falls back to 4_oseq_proj)
```

2) Create input data with the included gensort binary

This repo ships prebuilt `gensort` binaries under `gensort_libraries/32/` and `gensort_libraries/64/`.

- Ensure the output data directory exists:

```bash
mkdir -p sorting_project_data
```

- Make the gensort binary executable if needed and generate 100000 records:

```bash
chmod +x gensort_libraries/64/gensort
# Assumption: the included gensort accepts the common form: <N> <output-file>
./gensort_libraries/64/gensort 100000 sorting_project_data/data_100000.bin
```

Note: If the `64` binary is not appropriate for your platform, use `gensort_libraries/32/gensort` instead. If the included `gensort` on your system has different invocation flags, consult that binary's usage (e.g., `./gensort_libraries/64/gensort --help`).

3) Run a sorter (example: N = 100000)

Each C++ program expects an algorithm name and a filename. For example, run the basic sequential sorter with merge sort on the generated file:

```bash
# From repository root (binaries live in their subdirectories)
./1_bseq_proj/basic_seq_sorter merge_sort sorting_project_data/data_100000.bin

# Run the optimized sequential radix sorter
./2_oseq_proj/optimized_seq_sorter radix_sort sorting_project_data/data_100000.bin

# Run the basic OpenMP sorter (set OMP_NUM_THREADS as desired)
OMP_NUM_THREADS=4 ./3_bpar_proj/basic_omp_sorter merge_sort sorting_project_data/data_100000.bin

# Run the MPI-enabled optimized sorter (if you built MPI version)
mpirun -n 4 ./4_opar_proj/optimized_mpi_sorter radix_sort sorting_project_data/data_100000.bin
```

Each program prints a JSON result to stdout with fields like `N`, `time_ms`, `mkeys_per_s`, and `correct` so you can capture/pipe it to a file if needed:

```bash
./1_bseq_proj/basic_seq_sorter merge_sort sorting_project_data/data_100000.bin > run_basic_seq_100k.json
```

That's it — the above steps let you compile and run the C++ binaries directly without using any Python helper scripts.

## Expected Output

```
==========================================
   Full Project Workflow
==========================================

==> Step 1 of 3: Generating test data (if needed)...
    ✓ Test data ready

==> Step 2 of 3: Running experiments...
    ✓ Experiments finished successfully
    Results saved to: results.json

==> Step 3 of 3: Building report and generating plots...
    ✓ Report plots created
    Report directory: report/

==========================================
   Workflow Complete!
==========================================