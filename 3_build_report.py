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

RESULTS_FILE = Path("results.json")
REPORT_DIR = Path("report")


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
    
    # Validate required columns
    required_cols = ['algorithm', 'N', 'threads', 'time_ms']
    missing_cols = [col for col in required_cols if col not in df.columns]
    if missing_cols:
        print(f"Warning: Missing expected columns: {missing_cols}")
        print(f"Available columns: {list(df.columns)}")
        return
    
    # Calculate sequential baseline for speedup calculation
    # For each algorithm and dataset size (N), find the runtime
    # with 1 thread.
    sequential_base = df[df['threads'] == 1].copy()
    sequential_base = sequential_base.rename(
        columns={'time_ms': 'sequential_time'})
    sequential_base = sequential_base[['algorithm', 'N',
                                       'sequential_time']]
    
    # Merge sequential baseline back into the full DataFrame
    # to calculate speedup
    df = pd.merge(df, sequential_base,
                  on=['algorithm', 'N'], how='left')
    df['speedup'] = df['sequential_time'] / df['time_ms']
    
    print("--- Processed Data with Speedup Calculation ---")
    print(df.to_string())  # Print the full DataFrame for review

    sns.set_theme(style="whitegrid")  # Set a nice theme for plots
    
    # --- Plot 1: Scalability (Runtime vs. Threads) ---
    print("\nGenerating Scalability Plot...")
    g = sns.relplot(
        data=df,
        x='threads',
        y='time_ms',
        hue='N',  # Differentiate by dataset size
        col='algorithm',  # Separate plots for each algorithm
        kind='line',
        marker='o',
        height=5, aspect=1.2,
        facet_kws=dict(sharey=False, sharex=True)  # Independent Y-axis
    )
    g.set_axis_labels("Number of Threads", "Runtime (ms)")
    g.set_titles("Algorithm: {col_name}")
    g.fig.suptitle("Algorithm Scalability (Runtime vs. Threads)",
                   y=1.03)
    plt.tight_layout(rect=[0, 0, 1, 0.98])
    plt.savefig(REPORT_DIR / "scalability.png", dpi=300)
    plt.close()
    
    # --- Plot 2: Speedup vs. Threads ---
    print("Generating Speedup Plot...")
    g = sns.relplot(
        data=df,
        x='threads',
        y='speedup',
        hue='algorithm',
        style='N',  # Differentiate by N with different line styles
        kind='line',
        marker='o',
        height=6, aspect=1.5
    )
    # Add ideal speedup line
    thread_counts = sorted(df['threads'].unique())
    plt.plot(thread_counts, thread_counts, 'k--',
             label='Ideal Speedup')
    
    g.set_axis_labels("Number of Threads",
                      "Speedup (vs. 1 Thread)")
    plt.legend(title="Legend", loc='upper left',
               bbox_to_anchor=(1, 1))  # Move legend outside
    g.fig.suptitle("Parallel Speedup", y=1.03)
    plt.grid(True)
    plt.xlim(0, max(thread_counts) + 5)
    plt.ylim(0, max(thread_counts) * 1.1)
    plt.tight_layout(rect=[0, 0, 0.85, 0.98])
    plt.savefig(REPORT_DIR / "speedup.png", dpi=300)
    plt.close()

    # --- Plot 3: Throughput (Mega-Keys / sec) ---
    if 'mkeys_per_s' in df.columns:
        print("Generating Throughput Plot...")
        g = sns.relplot(
            data=df,
            x='threads',
            y='mkeys_per_s',
            hue='N',
            col='algorithm',
            kind='line',
            marker='o',
            height=5, aspect=1.2,
            facet_kws=dict(sharey=False, sharex=True)
        )
        g.set_axis_labels("Number of Threads",
                          "Throughput (Mega-Keys / sec)")
        g.set_titles("Algorithm: {col_name}")
        g.fig.suptitle("Algorithm Throughput vs. Core Count",
                       y=1.03)
        plt.tight_layout(rect=[0, 0, 1, 0.98])
        plt.savefig(REPORT_DIR / "throughput.png", dpi=300)
        plt.close()
    else:
        print("Warning: 'mkeys_per_s' column not found. "
              "Skipping throughput plot.")

    print(f"\n--- Report plots saved in '{REPORT_DIR}' "
          "directory ---")


if __name__ == "__main__":
    main()
