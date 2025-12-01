import json
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
import os

def load_data(filename='results.json'):
    with open(filename, 'r') as f:
        data = json.load(f)
    return pd.DataFrame(data)

def plot_scalability(df):
    """
    Plot Throughput vs. Input Size (N) for all sorters.
    """
    plt.figure(figsize=(12, 8))
    
    # Create a composite label for the legend
    df['Label'] = df['Title'] + ' (' + df['algorithm'] + ')'
    
    sns.lineplot(data=df, x='N', y='mkeys_per_s', hue='Label', style='Label', markers=True, dashes=False)
    
    plt.title('Scalability: Throughput vs. Input Size', fontsize=16)
    plt.xlabel('Input Size (N)', fontsize=14)
    plt.ylabel('Throughput (Million Keys/s)', fontsize=14)
    plt.xscale('log')
    plt.grid(True, which="both", ls="-", alpha=0.2)
    plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.tight_layout()
    plt.savefig('scalability_graph.png')
    print("Generated scalability_graph.png")

def plot_strong_scaling(df):
    """
    Plot Throughput vs. Threads for parallel sorters at a fixed large N.
    """
    # Filter for parallel sorters
    parallel_df = df[df['Title'].str.contains('Parallel')]
    
    if parallel_df.empty:
        print("No parallel data found for strong scaling.")
        return

    # Find the largest N common to most or just pick the max N available for each
    # Let's try to find a large N that has multiple thread counts
    max_n = parallel_df['N'].max()
    # It's possible different sorters have different max N. 
    # Let's pick the largest N that has at least 2 different thread counts.
    
    # For this specific project, we know we want to see scaling.
    # Let's filter for the largest N available in the dataset for each title/algo
    
    # Actually, let's just pick one large N, e.g., 160M or 40M if 160M not there.
    target_n = 80000000
    if target_n not in parallel_df['N'].values:
        target_n = 40000000 # Fallback
        
    scaling_df = parallel_df[parallel_df['N'] == target_n].copy()
    
    if scaling_df.empty:
        print(f"No data found for N={target_n} for strong scaling.")
        return

    plt.figure(figsize=(10, 6))
    scaling_df['Label'] = scaling_df['Title'] + ' (' + scaling_df['algorithm'] + ')'
    
    sns.lineplot(data=scaling_df, x='threads', y='mkeys_per_s', hue='Label', style='Label', markers=True, dashes=False)
    
    plt.title(f'Strong Scaling: Throughput vs. Threads (N={target_n:,})', fontsize=16)
    plt.xlabel('Total Threads', fontsize=14)
    plt.ylabel('Throughput (Million Keys/s)', fontsize=14)
    plt.grid(True, which="both", ls="-", alpha=0.2)
    plt.legend()
    plt.tight_layout()
    plt.savefig('strong_scaling_graph.png')
    print(f"Generated strong_scaling_graph.png (N={target_n})")

def plot_weak_scaling(df):
    """
    Plot Throughput vs. Threads where N/Threads is constant.
    Target constant: 2.5 Million items per thread (10M/4, 40M/16, 160M/64).
    """
    # Filter for Basic Parallel Sorter as identified in the plan
    target_sorter = "Basic Parallel Sorter"
    ws_df = df[df['Title'] == target_sorter].copy()
    
    if ws_df.empty:
        print("No Basic Parallel Sorter data found for weak scaling.")
        return

    # Calculate items per thread
    ws_df['items_per_thread'] = ws_df['N'] / ws_df['threads']
    
    # We are looking for approximately 2.5M items per thread
    # Allow for some small variation if integer division wasn't perfect, but here it should be exact
    target_load = 2500000
    
    # Filter rows where items_per_thread is close to target_load
    weak_scaling_data = ws_df[ws_df['items_per_thread'] == target_load].copy()
    
    if weak_scaling_data.empty:
        print("No matching data points (N/P = 2.5M) found for weak scaling.")
        return

    plt.figure(figsize=(10, 6))
    
    sns.lineplot(data=weak_scaling_data, x='threads', y='mkeys_per_s', hue='algorithm', style='algorithm', markers=True, dashes=False)
    
    plt.title(f'Weak Scaling: Throughput vs. Threads (Load ~ {target_load/1e6}M items/thread)', fontsize=16)
    plt.xlabel('Total Threads', fontsize=14)
    plt.ylabel('Throughput (Million Keys/s)', fontsize=14)
    plt.ylim(0, weak_scaling_data['mkeys_per_s'].max() * 1.2) # Start y-axis at 0 to show ideal flat line better
    plt.grid(True, which="both", ls="-", alpha=0.2)
    plt.legend(title='Algorithm')
    plt.tight_layout()
    plt.savefig('weak_scaling_graph.png')
    print("Generated weak_scaling_graph.png")

def main():
    if not os.path.exists('results.json'):
        print("results.json not found!")
        return

    df = load_data()
    
    # Ensure numeric types
    df['N'] = pd.to_numeric(df['N'])
    df['threads'] = pd.to_numeric(df['threads'])
    df['mkeys_per_s'] = pd.to_numeric(df['mkeys_per_s'])

    plot_scalability(df)
    plot_strong_scaling(df)
    plot_weak_scaling(df)

if __name__ == "__main__":
    main()
