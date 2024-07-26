import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import argparse
from datetime import datetime, timedelta
import numpy as np

def plot_neural_data(csv_file):
    # Read the CSV file
    df = pd.read_csv(csv_file)
    
    # Convert timestamp to datetime
    start_time = datetime.now() - timedelta(milliseconds=int(df['timestamp'].max()))
    df['time'] = [start_time + timedelta(milliseconds=int(ts)) for ts in df['timestamp']]
    
    # Create a figure with subplots
    fig, axs = plt.subplots(4, 4, figsize=(20, 15), sharex=True)
    fig.suptitle('Neural Data Channels', fontsize=16)
    
    # Plot each channel
    for i in range(16):
        row = i // 4
        col = i % 4
        axs[row, col].plot(df['time'], df[f'ch{i+1}'])
        axs[row, col].set_title(f'Channel {i+1}')
        axs[row, col].grid(True)
    
    # Format x-axis
    for ax in axs.flat:
        ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
        ax.xaxis.set_major_locator(mdates.AutoDateLocator())
    
    # Adjust layout and display
    plt.tight_layout()
    fig.autofmt_xdate()  # Rotate and align the tick labels
    plt.show()

def main():
    parser = argparse.ArgumentParser(description='Plot neural data from CSV file.')
    parser.add_argument('input_file', help='Path to the input CSV file')
    args = parser.parse_args()

    plot_neural_data(args.input_file)

if __name__ == "__main__":
    main()