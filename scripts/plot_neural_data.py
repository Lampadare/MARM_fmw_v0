import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import argparse
from datetime import datetime, timedelta
import numpy as np
from matplotlib.ticker import AutoMinorLocator, MultipleLocator

def plot_neural_data(csv_file):
    # Read the CSV file
    df = pd.read_csv(csv_file)
    
    # Convert timestamp to seconds
    df['time'] = df['timestamp'] / 1000  # Convert milliseconds to seconds
    
    # Plot 1: All channels
    fig1, axs = plt.subplots(4, 4, figsize=(20, 15), sharex=True)
    fig1.suptitle('All Neural Data Channels', fontsize=16)
    
    # Plot each channel
    for i in range(16):
        row = i // 4
        col = i % 4
        axs[row, col].plot(df['time'], df[f'ch{i+1}'])
        axs[row, col].set_title(f'Channel {i+1}')
        axs[row, col].grid(True)
        axs[row, col].set_ylabel('Amplitude (μV)')
    
    # Format x-axis for all subplots
    for ax in axs.flat:
        ax.xaxis.set_major_locator(MultipleLocator(0.01))  # Major tick every 10ms
        ax.xaxis.set_minor_locator(AutoMinorLocator(2))  # 2 minor ticks between major ticks
        ax.tick_params(axis='x', rotation=45)
    
    # Add common x-label
    fig1.text(0.5, 0.04, 'Time (seconds)', ha='center', va='center')
    
    # Adjust layout and display
    plt.tight_layout()
    
    # Plot 2: Only Channel 10
    fig2, ax = plt.subplots(figsize=(15, 5))
    ax.plot(df['time'], df['ch1'])
    ax.set_title('Channel 1 Data', fontsize=16)
    ax.grid(True)
    
    # Format x-axis for Channel 10 plot
    ax.xaxis.set_major_locator(MultipleLocator(0.01))  # Major tick every 10ms
    ax.xaxis.set_minor_locator(AutoMinorLocator(2))  # 2 minor ticks between major ticks
    ax.tick_params(axis='x', rotation=45)
    
    # Add axis labels
    ax.set_xlabel('Time (seconds)')
    ax.set_ylabel('Amplitude (μV)')
    
    # Adjust layout
    plt.tight_layout()
    
    # Show both plots
    plt.show()

def main():
    parser = argparse.ArgumentParser(description='Plot neural data from CSV file.')
    parser.add_argument('input_file', help='Path to the input CSV file')
    args = parser.parse_args()

    plot_neural_data(args.input_file)

if __name__ == "__main__":
    main()