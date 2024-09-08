import pandas as pd
import matplotlib.pyplot as plt
import argparse
from matplotlib.ticker import AutoMinorLocator, MultipleLocator
import numpy as np

def plot_chunk(ax, time, data, channel, color):
    ax.plot(time, data, color=color, linewidth=0.5, alpha=0.7)  # Line plot with some transparency
    ax.scatter(time, data, color=color, s=1, alpha=0.7)  # Scatter plot for individual points
    ax.set_title(f'Channel {channel}')
    ax.grid(True)
    ax.set_ylabel('Amplitude (μV)')

def plot_neural_data(csv_file, chunk_size=1000000, duration=1):
    # Create subplots
    fig1, axs = plt.subplots(4, 4, figsize=(20, 15), sharex=True)
    fig1.suptitle('All Neural Data Channels (Last 1 second)', fontsize=16)
    
    # Flatten axs for easier indexing
    axs_flat = axs.flatten()
    
    # Create a separate plot for Channel 10
    fig2, ax_ch10 = plt.subplots(figsize=(15, 5))
    
    # First pass: determine the total duration of the data and calculate average spacing
    total_duration = 0
    total_points = 0
    prev_time = None
    sum_spacing = 0
    
    for chunk in pd.read_csv(csv_file, chunksize=chunk_size):
        chunk['time'] = chunk['timestamp'] / 1000  # Convert milliseconds to seconds
        total_duration = max(total_duration, chunk['time'].max())
        total_points += len(chunk)
        
        if prev_time is not None:
            sum_spacing += chunk['time'].iloc[0] - prev_time
        
        if len(chunk) > 1:
            sum_spacing += chunk['time'].diff().sum()
        
        prev_time = chunk['time'].iloc[-1]
    
    avg_spacing = sum_spacing / (total_points - 1) if total_points > 1 else 0
    avg_spacing_ms = avg_spacing * 1000  # Convert to milliseconds
    
    # Calculate the start time for the last 1 second
    start_time = max(0, total_duration - duration)
    
    # Second pass: plot the last 1 second of data
    for chunk in pd.read_csv(csv_file, chunksize=chunk_size):
        chunk['time'] = chunk['timestamp'] / 1000  # Convert milliseconds to seconds
        
        # Only keep data from the last 1 second
        chunk = chunk[chunk['time'] >= start_time]
        
        if chunk.empty:
            continue
        
        # Plot each channel
        for i in range(16):
            plot_chunk(axs_flat[i], chunk['time'], chunk[f'ch{i+1}'], i+1, 'b')
        
        # Plot Channel 10 separately
        plot_chunk(ax_ch10, chunk['time'], chunk['ch11'], 10, 'r')
    
    # Set x-axis limits and format for all plots
    for ax in axs_flat:
        ax.set_xlim(start_time, total_duration)
        ax.xaxis.set_major_locator(MultipleLocator(0.2))  # Major tick every 0.2 seconds
        ax.xaxis.set_minor_locator(AutoMinorLocator(2))  # 2 minor ticks between major ticks
        ax.tick_params(axis='x', rotation=45)
    
    # Set x-axis limits and format for Channel 10 plot
    ax_ch10.set_xlim(start_time, total_duration)
    ax_ch10.xaxis.set_major_locator(MultipleLocator(0.2))  # Major tick every 0.2 seconds
    ax_ch10.xaxis.set_minor_locator(AutoMinorLocator(2))  # 2 minor ticks between major ticks
    ax_ch10.tick_params(axis='x', rotation=45)
    
    # Add labels
    fig1.text(0.5, 0.04, 'Time (seconds)', ha='center', va='center')
    ax_ch10.set_xlabel('Time (seconds)')
    ax_ch10.set_ylabel('Amplitude (μV)')
    ax_ch10.set_title('Channel 10 Data (Last 1 second)', fontsize=16)
    
    # Add caption with average spacing
    caption = f'Average spacing between data points: {avg_spacing_ms:.3f} ms'
    fig1.text(0.5, 0.01, caption, ha='center', va='center', fontsize=10)
    fig2.text(0.5, 0.01, caption, ha='center', va='center', fontsize=10)
    
    # Adjust layout
    fig1.tight_layout()
    fig2.tight_layout()
    
    # Show plots
    plt.show()

def main():
    parser = argparse.ArgumentParser(description='Plot large neural data from CSV file (last 1 second).')
    parser.add_argument('input_file', help='Path to the input CSV file')
    args = parser.parse_args()

    plot_neural_data(args.input_file)

if __name__ == "__main__":
    main()