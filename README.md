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