import json
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

# --- Configuration ---
RESULTS_FILE = Path("results.json")
REPORT_DIR = Path("report")

def main():
    if not RESULTS_FILE.exists():
        print(f"Error: Results file '{RESULTS_FILE}' not found.")
        print("Please run 'run_all.sh' or 'run_experiments.py' first.")
        return

    REPORT_DIR.mkdir(exist_ok=True)
    
    df = pd.read_json(RESULTS_FILE)
    
    if df.empty:
        print("Results file is empty. No report generated.")
        return
        
    # --- Data Processing: Calculate Speedup ---
    sequential_times = df[df['threads'] == 1].copy()
    seq_introsort = sequential_times[sequential_times['algorithm'] == 'introsort']
    if not seq_introsort.empty:
        best_seq_cutoff_idx = seq_introsort.loc[seq_introsort.groupby('N')['time_ms'].idxmin()]
    else:
        best_seq_cutoff_idx = pd.DataFrame()
    
    seq_radix = sequential_times[sequential_times['algorithm'] == 'radix_sort']
    
    sequential_base = pd.concat([best_seq_cutoff_idx, seq_radix])
    sequential_base = sequential_base.rename(columns={'time_ms': 'sequential_time'})
    sequential_base = sequential_base[['algorithm', 'N', 'sequential_time']]
    
    df = pd.merge(df, sequential_base, on=['algorithm', 'N'])
    df['speedup'] = df['sequential_time'] / df['time_ms']
    
    print("--- Data with Speedup Calculation ---")
    print(df.to_string())

    # --- Plotting ---
    sns.set_theme(style="whitegrid")

    # 1. Scalability Plot: Runtime vs. Threads
    df_best_cutoff = df.loc[df.groupby(['algorithm', 'N', 'threads'])['time_ms'].idxmin()]
    print("\nGenerating Scalability Plot...")
    g = sns.relplot(
        data=df_best_cutoff, x='threads', y='time_ms',
        hue='N', col='algorithm',
        kind='line', marker='o', facet_kws=dict(sharey=False)
    )
    g.set_axis_labels("Number of Threads", "Runtime (ms)")
    g.fig.suptitle("Algorithm Scalability", y=1.03)
    plt.savefig(REPORT_DIR / "scalability.png", dpi=300)
    plt.close()

    # 2. Speedup Plot
    print("Generating Speedup Plot...")
    g = sns.relplot(
        data=df_best_cutoff, x='threads', y='speedup',
        hue='algorithm', style='N', kind='line', marker='o'
    )
    thread_counts = sorted(df['threads'].unique())
    plt.plot(thread_counts, thread_counts, 'k--', label='Ideal Speedup')
    g.set_axis_labels("Number of Threads", "Speedup")
    plt.legend()
    g.fig.suptitle("Parallel Speedup", y=1.03)
    plt.savefig(REPORT_DIR / "speedup.png", dpi=300)
    plt.close()

    # 3. Introsort Cutoff Analysis Plot
    print("Generating Introsort Cutoff Analysis Plot...")
    introsort_df = df[df['algorithm'] == 'introsort']
    if not introsort_df.empty:
        g = sns.relplot(
            data=introsort_df, x='cutoff', y='time_ms',
            hue='threads', palette='viridis', col='N',
            kind='line', marker='o', facet_kws=dict(sharey=False)
        )
        g.set_axis_labels("Sequential Cutoff Threshold", "Runtime (ms)")
        g.fig.suptitle("Introsort Task Granularity Analysis", y=1.03)
        plt.savefig(REPORT_DIR / "introsort_cutoff_analysis.png", dpi=300)
        plt.close()

    print(f"\n--- Report plots saved in '{REPORT_DIR}' directory ---")

if __name__ == "__main__":
    main()