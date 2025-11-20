import json
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path

RESULTS_FILE = Path("results.json")
REPORT_DIR = Path("report")

def main():
    if not RESULTS_FILE.exists():
        print(f"Error: Results file '{RESULTS_FILE}' not found at {RESULTS_FILE.resolve()}.")
        print("Please run './run_all.sh' or 'python3 run_experiments.py' first.")
        return

    REPORT_DIR.mkdir(exist_ok=True) # Ensure report directory exists

    df = pd.read_json(RESULTS_FILE)
    if df.empty:
        print("Results file is empty or contains no valid JSON data. No report generated.")
        return
    
    # Calculate sequential baseline for speedup calculation
    # For each algorithm and dataset size (N), find the runtime with 1 thread.
    sequential_base = df[df['threads'] == 1].copy()
    sequential_base = sequential_base.rename(columns={'time_ms': 'sequential_time'})
    sequential_base = sequential_base[['algorithm', 'N', 'sequential_time']]
    
    # Merge sequential baseline back into the full DataFrame to calculate speedup
    df = pd.merge(df, sequential_base, on=['algorithm', 'N'], how='left')
    df['speedup'] = df['sequential_time'] / df['time_ms']
    
    print("--- Processed Data with Speedup Calculation ---")
    print(df.to_string()) # Print the full DataFrame for review

    sns.set_theme(style="whitegrid") # Set a nice theme for plots
    
    # --- Plot 1: Scalability (Runtime vs. Threads) ---
    print("\nGenerating Scalability Plot...")
    g = sns.relplot(
        data=df, 
        x='threads', 
        y='time_ms', 
        hue='N',         # Differentiate by dataset size if multiple are present
        col='algorithm', # Separate plots for each algorithm
        kind='line', 
        marker='o', 
        height=5, aspect=1.2,
        facet_kws=dict(sharey=False, sharex=True) # Independent Y-axis, shared X-axis
    )
    g.set_axis_labels("Number of Threads", "Runtime (ms)")
    g.set_titles("Algorithm: {col_name}")
    g.fig.suptitle("Algorithm Scalability (Runtime vs. Threads)", y=1.03)
    plt.tight_layout(rect=[0, 0, 1, 0.98]) # Adjust layout to prevent title overlap
    plt.savefig(REPORT_DIR / "scalability.png", dpi=300)
    plt.close()
    
    # --- Plot 2: Speedup vs. Threads ---
    print("Generating Speedup Plot...")
    g = sns.relplot(
        data=df, 
        x='threads', 
        y='speedup', 
        hue='algorithm', 
        style='N',       # Differentiate by N with different line styles
        kind='line', 
        marker='o',
        height=6, aspect=1.5
    )
    # Add ideal speedup line
    thread_counts = sorted(df['threads'].unique())
    plt.plot(thread_counts, thread_counts, 'k--', label='Ideal Speedup')
    
    g.set_axis_labels("Number of Threads", "Speedup (vs. 1 Thread)")
    plt.legend(title="Legend", loc='upper left', bbox_to_anchor=(1,1)) # Move legend outside
    g.fig.suptitle("Parallel Speedup", y=1.03)
    plt.grid(True)
    plt.xlim(0, max(thread_counts) + 5)
    plt.ylim(0, max(thread_counts) * 1.1)
    plt.tight_layout(rect=[0, 0, 0.85, 0.98]) # Adjust layout for legend
    plt.savefig(REPORT_DIR / "speedup.png", dpi=300)
    plt.close()

    # --- Plot 3: Throughput (Mega-Keys / sec) ---
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
    g.set_axis_labels("Number of Threads", "Throughput (Mega-Keys / sec)")
    g.set_titles("Algorithm: {col_name}")
    g.fig.suptitle("Algorithm Throughput vs. Core Count", y=1.03)
    plt.tight_layout(rect=[0, 0, 1, 0.98])
    plt.savefig(REPORT_DIR / "throughput.png", dpi=300)
    plt.close()

    print(f"\n--- Report plots saved in '{REPORT_DIR}' directory ---")


if __name__ == "__main__":
    main()