#!/usr/bin/env python3
"""
Build analysis report from experiment results.

Prerequisites:
  - 1_generate_data.py must be run first to generate test data
  - 2_run_experiments.py must be run to generate results.json

Expected results.json format (array of JSON objects):
  [
    {
      "algorithm": "basic_merge_sort",
      "N": 50000000,
      "threads": 1,
      "time_ms": 1234.56,
      "mkeys_per_s": 40.53,
      "correct": true
    },
    ...
  ]
"""

import json
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
import sys
import math

RESULTS_FILE = Path("results.json")
REPORT_DIR = Path("report")


def _safe_int(x):
    try:
        return int(x)
    except Exception:
        return None


def normalize_threads(df: pd.DataFrame) -> pd.DataFrame:
    """Ensure a `total_threads` column exists by deriving from
    available fields: `threads`, or `mpi_procs * omp_threads`, or
    `omp_threads` / `mpi_procs` if that's all that's available.
    """
    def comp(row):
        # Prefer explicit `threads` if present and valid
        if 'threads' in row and pd.notna(row['threads']):
            t = _safe_int(row['threads'])
            if t and t > 0:
                return t

        # Hybrid MPI+OMP
        if 'mpi_procs' in row and 'omp_threads' in row:
            if pd.notna(row['mpi_procs']) and pd.notna(row['omp_threads']):
                mpi = _safe_int(row['mpi_procs'])
                omp = _safe_int(row['omp_threads'])
                if mpi and omp:
                    return mpi * omp

        # Fallback to either one if present
        if 'omp_threads' in row and pd.notna(row['omp_threads']):
            omp = _safe_int(row['omp_threads'])
            if omp and omp > 0:
                return omp
        if 'mpi_procs' in row and pd.notna(row['mpi_procs']):
            mpi = _safe_int(row['mpi_procs'])
            if mpi and mpi > 0:
                return mpi

        return None

    df = df.copy()
    df['total_threads'] = df.apply(comp, axis=1)
    missing = df['total_threads'].isna().sum()
    if missing > 0:
        print(f"Warning: {missing} rows have no computable total_threads; these will be dropped for scaling plots.")
    df = df[df['total_threads'].notna()].copy()
    df['total_threads'] = df['total_threads'].astype(int)
    return df


def main():
    """Generate analysis plots from experiment results."""
    if not RESULTS_FILE.exists():
        error_msg = f"Error: Results file '{RESULTS_FILE}' not found"
        print(error_msg)
        print(f"Location: {RESULTS_FILE.resolve()}")
        print("Please run './run_all.sh' or 'python3 "
              "2_run_experiments.py' first.")
        return

    REPORT_DIR.mkdir(exist_ok=True)  # Ensure report directory exists

    try:
        df = pd.read_json(RESULTS_FILE)
    except Exception as e:
        print(f"Error reading JSON file: {e}")
        return

    if df.empty:
        msg = ("Results file is empty or contains no valid JSON "
               "data. No report generated.")
        print(msg)
        return
    
    # Basic validation
    if 'algorithm' not in df.columns:
        print("Error: 'algorithm' column missing in results.json")
        return
    if 'N' not in df.columns or 'time_ms' not in df.columns:
        print("Error: required columns 'N' and/or 'time_ms' missing")
        return

    # Ensure there is a Title column for grouping; if not, create one
    if 'Title' not in df.columns:
        df['Title'] = df['algorithm']

    # Normalize thread counts into `total_threads`
    df = normalize_threads(df)
    if df.empty:
        print("No rows left after normalizing thread counts — cannot build scaling plots.")
        return
    
    # Calculate sequential baseline for speedup calculation.
    # Use groups by Title+algorithm+N so we compare like-for-like builds.
    seq_base = df[df['total_threads'] == 1][['Title', 'algorithm', 'N', 'time_ms']].copy()
    seq_base = seq_base.rename(columns={'time_ms': 'sequential_time'})

    # If baseline missing for some group (no 1-thread row), use the
    # minimum-time entry for that group as a fallback baseline.
    groups = []
    for (title, alg, N), group in df.groupby(['Title', 'algorithm', 'N']):
        if not ((seq_base['Title'] == title) & (seq_base['algorithm'] == alg) & (seq_base['N'] == N)).any():
            # pick the row with smallest total_threads (closest to sequential)
            candidate = group.loc[group['total_threads'].idxmin()]
            groups.append({'Title': title, 'algorithm': alg, 'N': N, 'sequential_time': candidate['time_ms']})
    if groups:
        fallback = pd.DataFrame(groups)
        seq_base = pd.concat([seq_base, fallback], ignore_index=True)

    df = pd.merge(df, seq_base, on=['Title', 'algorithm', 'N'], how='left')
    df['speedup'] = df['sequential_time'] / df['time_ms']
    df['efficiency'] = df['speedup'] / df['total_threads']
    
    print("--- Processed Data with Speedup Calculation (sample) ---")
    print(df.head(40).to_string())  # Print a sample of the DataFrame for review

    sns.set_theme(style="whitegrid")  # Set a nice theme for plots

    plots_dir = REPORT_DIR / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    # --- Strong scaling plots per Title+algorithm ---
    print("\nGenerating Strong Scaling Plots...")
    for (title, alg), group in df.groupby(['Title', 'algorithm']):
        tname = f"{title}__{alg}".replace(' ', '_').replace('/', '_')
        # runtime vs threads for each N
        plt.figure(figsize=(8, 5))
        sns.lineplot(data=group, x='total_threads', y='time_ms', hue='N', marker='o')
        plt.xscale('log', base=2)
        plt.xlabel('Total Threads')
        plt.ylabel('Runtime (ms)')
        plt.title(f"{title} - {alg} : Runtime vs Total Threads")
        plt.tight_layout()
        plt.savefig(plots_dir / f"{tname}_runtime_vs_threads.png", dpi=300)
        plt.close()

        # speedup vs threads
        plt.figure(figsize=(8, 5))
        sns.lineplot(data=group, x='total_threads', y='speedup', hue='N', marker='o')
        # ideal speedup
        max_t = group['total_threads'].max()
        xs = sorted(group['total_threads'].unique())
        plt.plot(xs, xs, 'k--', label='Ideal')
        plt.xscale('log', base=2)
        plt.xlabel('Total Threads')
        plt.ylabel('Speedup (vs. sequential)')
        plt.title(f"{title} - {alg} : Speedup vs Total Threads")
        plt.legend()
        plt.tight_layout()
        plt.savefig(plots_dir / f"{tname}_speedup_vs_threads.png", dpi=300)
        plt.close()

        # efficiency vs threads
        plt.figure(figsize=(8, 5))
        sns.lineplot(data=group, x='total_threads', y='efficiency', hue='N', marker='o')
        plt.xscale('log', base=2)
        plt.xlabel('Total Threads')
        plt.ylabel('Parallel Efficiency')
        plt.title(f"{title} - {alg} : Efficiency vs Total Threads")
        plt.tight_layout()
        plt.savefig(plots_dir / f"{tname}_efficiency_vs_threads.png", dpi=300)
        plt.close()

        # throughput if available
        if 'mkeys_per_s' in group.columns:
            plt.figure(figsize=(8, 5))
            sns.lineplot(data=group, x='total_threads', y='mkeys_per_s', hue='N', marker='o')
            plt.xscale('log', base=2)
            plt.xlabel('Total Threads')
            plt.ylabel('Throughput (Mega-Keys / s)')
            plt.title(f"{title} - {alg} : Throughput vs Total Threads")
            plt.tight_layout()
            plt.savefig(plots_dir / f"{tname}_throughput_vs_threads.png", dpi=300)
            plt.close()

    # --- Weak scaling analysis ---
    print("Generating Weak Scaling Plots (by work per thread)...")
    # compute work per thread and round to nearest sensible value
    df['work_per_thread'] = (df['N'] / df['total_threads']).apply(lambda x: int(round(x)) if pd.notna(x) else None)
    # consider work sizes that appear with at least two different thread counts
    for (title, alg), group in df.groupby(['Title', 'algorithm']):
        tname = f"{title}__{alg}".replace(' ', '_').replace('/', '_')
        wp_counts = group.groupby('work_per_thread')['total_threads'].nunique()
        candidate_wp = wp_counts[wp_counts >= 2].index.dropna()
        for wp in candidate_wp:
            sub = group[group['work_per_thread'] == wp]
            if sub.empty:
                continue
            plt.figure(figsize=(8, 5))
            sns.lineplot(data=sub, x='total_threads', y='time_ms', marker='o')
            plt.xscale('log', base=2)
            plt.xlabel('Total Threads')
            plt.ylabel('Runtime (ms)')
            plt.title(f"{title} - {alg} : Weak Scaling (work/thread={wp})")
            plt.tight_layout()
            plt.savefig(plots_dir / f"{tname}_weak_time_wpt{wp}.png", dpi=300)
            plt.close()

            if 'mkeys_per_s' in sub.columns:
                plt.figure(figsize=(8, 5))
                sns.lineplot(data=sub, x='total_threads', y='mkeys_per_s', marker='o')
                plt.xscale('log', base=2)
                plt.xlabel('Total Threads')
                plt.ylabel('Throughput (Mega-Keys / s)')
                plt.title(f"{title} - {alg} : Weak Scaling Throughput (work/thread={wp})")
                plt.tight_layout()
                plt.savefig(plots_dir / f"{tname}_weak_throughput_wpt{wp}.png", dpi=300)
                plt.close()

    # --- Weak scaling analysis with grid subplots ---
    print("Generating Weak Scaling Grid Subplots (by work per thread)...")
    for (title, alg), group in df.groupby(['Title', 'algorithm']):
        tname = f"{title}__{alg}".replace(' ', '_').replace('/', '_')
        wp_counts = group.groupby('work_per_thread')['total_threads'].nunique()
        candidate_wp = wp_counts[wp_counts >= 2].index.dropna()

        if not candidate_wp.empty:
            num_plots = len(candidate_wp)
            grid_size = math.ceil(math.sqrt(num_plots))
            fig, axes = plt.subplots(grid_size, grid_size, figsize=(5 * grid_size, 5 * grid_size), sharex=True, sharey=True)
            axes = axes.flatten()

            for ax, wp in zip(axes, candidate_wp):
                sub = group[group['work_per_thread'] == wp]
                if sub.empty:
                    continue
                sns.lineplot(data=sub, x='total_threads', y='time_ms', marker='o', ax=ax)
                ax.set_xscale('log', base=2)
                ax.set_xlabel('Total Threads')
                ax.set_ylabel('Runtime (ms)')
                ax.set_title(f"Work/Thread={wp}")

            # Hide unused subplots
            for ax in axes[len(candidate_wp):]:
                ax.axis('off')

            plt.tight_layout()
            plt.savefig(plots_dir / f"{tname}_weak_scaling_grid_subplots.png", dpi=300)
            plt.close()

            print(f"Saved weak scaling grid subplots for {title} - {alg}")

    print(f"\n--- Report plots saved in '{plots_dir}' directory ---")


if __name__ == "__main__":
    main()
