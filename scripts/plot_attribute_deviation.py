import re
import argparse
from datetime import datetime, timedelta
import matplotlib.pyplot as plt
import pandas as pd
import os
from typing import List, Tuple, Dict
from matplotlib.ticker import MultipleLocator, FormatStrFormatter

# Configure matplotlib style
plt.style.use('seaborn-darkgrid')

# Constants
EXPECTED_INTERVAL_MS = 7.5     # Expected interval in milliseconds
SNIPPET_DURATION_SECONDS = 10  # Duration of the snippet to analyze

# Define color mapping for configurations with differentiation between SD and BLE within groups
CONFIG_COLORS = {
    # Intan Configurations
    'BLE+SD (Intan) SD': '#1f77b4',      # Dark Blue
    'BLE+SD (Intan) BLE': '#54d1ff',     # Light Blue
    'BLE (Intan)': '#2ca02c',            # Green
    'SD (Intan)': '#9467bd',             # Purple
}

# Define BLE configurations for marker style differentiation
BLE_CONFIGS = [
    'BLE+SD (Intan) BLE',
    'BLE (Intan)'
]

def create_failed_csv(output_path: str):
    """
    Create a CSV file indicating a failed processing case.

    Args:
        output_path (str): Path to the failed CSV file.
    """
    try:
        df = pd.DataFrame({'status': ['failed']})
        df.to_csv(output_path, index=False)
        print(f"Created failed CSV: {output_path}")
    except Exception as e:
        print(f"Error creating failed CSV {output_path}: {e}")

def parse_log_file(log_file: str, snippet_duration: int = SNIPPET_DURATION_SECONDS) -> List[datetime]:
    """
    Parse a single log file to extract timestamps of "Attribute value changed" events within a 10-second snippet in the middle.

    Args:
        log_file (str): Path to the log file.
        snippet_duration (int): Duration of the snippet in seconds to analyze.

    Returns:
        List[datetime]: List of datetime objects representing event timestamps within the snippet.
    """
    print(f"Parsing log file: {log_file}")
    timestamps = []
    # Regex pattern to capture the timestamp at the beginning of the line
    # Example line:
    # 2024-09-14T21:29:24.282Z INFO Attribute value changed, handle: 0x12, value (0x): ...
    pattern = r'^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z).*INFO Attribute value changed'

    try:
        with open(log_file, 'r') as file:
            all_events = []
            for line in file:
                match = re.search(pattern, line)
                if match:
                    timestamp_str = match.group(1)
                    try:
                        timestamp = datetime.strptime(timestamp_str, '%Y-%m-%dT%H:%M:%S.%fZ')
                        all_events.append(timestamp)
                    except ValueError as ve:
                        print(f"Failed to parse timestamp '{timestamp_str}' in file {log_file}: {ve}")

        if not all_events:
            print(f"No 'Attribute value changed' events found in {log_file}.")
            return []

        # Determine the total duration of the log
        start_time = all_events[0]
        end_time = all_events[-1]
        total_duration = (end_time - start_time).total_seconds()

        if total_duration <= snippet_duration:
            print(f"Total duration {total_duration}s is less than or equal to snippet duration {snippet_duration}s in {log_file}. Analyzing entire file.")
            return all_events

        # Calculate the start and end times for the snippet
        snippet_start = start_time + timedelta(seconds=(total_duration / 2) - (snippet_duration / 2))
        snippet_end = snippet_start + timedelta(seconds=snippet_duration)

        # Extract events within the snippet
        for timestamp in all_events:
            if snippet_start <= timestamp <= snippet_end:
                timestamps.append(timestamp)

        print(f"Extracted {len(timestamps)} 'Attribute value changed' events within the 10s snippet from {log_file}")
    except FileNotFoundError:
        print(f"Log file not found: {log_file}")
    except Exception as e:
        print(f"Error parsing log file {log_file}: {e}")

    return timestamps

def process_files(base_folder: str, output_folder: str) -> List[Tuple[int, str, str]]:
    """
    Process all files based on the provided file mapping.

    Args:
        base_folder (str): Path to the base folder containing all data files.
        output_folder (str): Path to the output folder for processed CSV files.

    Returns:
        List[Tuple[int, str, str]]: List of tuples containing (frequency, configuration, output_csv_path)
    """
    # Define only the Intan configurations up to 1200 Hz
    file_mapping = {
        100: {
            'BLE+SD (Intan) SD': 'session_1',
            'BLE+SD (Intan) BLE': 'b1.txt',
            'BLE (Intan)': 'b10.txt',
            'SD (Intan)': 'session_17',
        },
        250: {
            'BLE+SD (Intan) SD': 'session_2',
            'BLE+SD (Intan) BLE': 'b2.txt',
            'BLE (Intan)': 'b11.txt',
            'SD (Intan)': 'session_18',
        },
        500: {
            'BLE+SD (Intan) SD': 'session_3',
            'BLE+SD (Intan) BLE': 'b3.txt',
            'BLE (Intan)': 'b12.txt',
            'SD (Intan)': 'session_19',
        },
        750: {
            'BLE+SD (Intan) SD': 'session_4',
            'BLE+SD (Intan) BLE': 'b4.txt',
            'BLE (Intan)': 'b13.txt',
            'SD (Intan)': 'session_20',
        },
        1000: {
            'BLE+SD (Intan) SD': 'session_5',
            'BLE+SD (Intan) BLE': 'b5.txt',
            'BLE (Intan)': 'b14.txt',
            'SD (Intan)': 'session_21',
        },
        1200: {
            'BLE+SD (Intan) SD': 'session_6',
            'BLE+SD (Intan) BLE': 'b6.txt',
            'BLE (Intan)': 'b15.txt',
            'SD (Intan)': 'session_22',
        },
    }

    # Define failed cases (only for specific frequencies and configurations)
    failed_cases = [
        (1500, 'BLE+SD (Intan) SD'), (1500, 'BLE+SD (Intan) BLE'),
        (1500, 'BLE (Intan)'), (1500, 'SD (Intan)'),
        (2000, 'BLE+SD (Intan) SD'), (2000, 'BLE+SD (Intan) BLE'),
        (2000, 'BLE (Intan)'), (2000, 'SD (Intan)'),
        (2500, 'BLE+SD (Intan) SD'), (2500, 'BLE+SD (Intan) BLE'),
        (2500, 'BLE (Intan)'), (2500, 'SD (Intan)')
    ]

    processed_files = []

    for freq, configs in file_mapping.items():
        for config, filename in configs.items():
            input_path = os.path.join(base_folder, filename)
            sanitized_config = config.replace(' ', '_').replace('(', '').replace(')', '').replace('+', 'plus')
            output_csv = os.path.join(output_folder, f"{freq}_{sanitized_config}.csv")

            if (freq, config) in failed_cases:
                create_failed_csv(output_csv)
                print(f"Created expected failed CSV for {freq} Hz, {config}")
                continue  # Skip processing for failed cases

            if os.path.exists(input_path):
                if os.path.isfile(input_path):
                    # This is a BLE .txt file
                    print(f"Processing BLE file: {input_path}")
                    timestamps = parse_log_file(input_path)
                    if timestamps:
                        df = pd.DataFrame({'timestamp': timestamps})
                        df.to_csv(output_csv, index=False)
                        print(f"Saved extracted timestamps to {output_csv}")
                    else:
                        print(f"No 'Attribute value changed' events found in {input_path}. Creating failed CSV.")
                        create_failed_csv(output_csv)
                else:
                    # If it's not a file, handle accordingly (e.g., directories)
                    print(f"Expected a file but found a different type: {input_path}. Creating failed CSV.")
                    create_failed_csv(output_csv)
            else:
                print(f"File not found: {input_path}. Creating failed CSV.")
                create_failed_csv(output_csv)

            # Append to processed_files for interval analysis
            processed_files.append((freq, config, output_csv))

    print("\nProcessing Summary:")
    total_expected_files = sum(len(files) for files in file_mapping.values())
    total_processed = len(processed_files)
    total_expected_failed = len(failed_cases)
    print(f"Total expected files: {total_expected_files}")
    print(f"Total processed files: {total_processed}")
    print(f"Total expected failed files: {total_expected_failed}")
    # Note: You can add more detailed summaries if needed

    return processed_files

def calculate_average_interval(timestamps: List[datetime], expected_interval_ms: float = EXPECTED_INTERVAL_MS) -> pd.DataFrame:
    """
    Calculate the average connection interval between consecutive events.

    Args:
        timestamps (List[datetime]): List of event timestamps.
        expected_interval_ms (float): Expected interval in milliseconds.

    Returns:
        pd.DataFrame: DataFrame containing intervals and average interval.
    """
    print("Calculating average connection intervals.")
    if len(timestamps) < 2:
        print("Not enough events to calculate intervals.")
        return pd.DataFrame(columns=['Interval_ms'])

    # Convert timestamps to pandas Series for easier manipulation
    ts_series = pd.Series(timestamps)
    # Calculate the difference between consecutive timestamps in milliseconds
    intervals = ts_series.diff().dt.total_seconds().dropna() * 1000  # Convert to ms
    # Create a DataFrame
    df = pd.DataFrame({
        'Interval_ms': intervals
    })
    print(f"Calculated {len(df)} intervals.")
    return df

def plot_average_interval(interval_data: Dict[Tuple[int, str], pd.DataFrame], output_path: str):
    """
    Generate a plot showing the average connection intervals across all frequencies and configurations.

    Args:
        interval_data (Dict[Tuple[int, str], pd.DataFrame]): Dictionary with keys as (frequency, configuration)
            and values as DataFrames containing intervals.
        output_path (str): Path to save the generated graph.
    """
    print("Generating average connection interval plot.")
    plt.figure(figsize=(20, 12))  # Increased figure size for better readability

    # Calculate average interval per configuration
    config_avg_interval = {}
    for (freq, config), df in interval_data.items():
        if df.empty or 'Interval_ms' not in df.columns:
            continue
        avg_interval = df['Interval_ms'].mean()
        if config not in config_avg_interval:
            config_avg_interval[config] = {}
        config_avg_interval[config][freq] = avg_interval

    # Prepare data for plotting
    configurations = list(config_avg_interval.keys())
    frequencies = sorted({freq for (freq, _) in interval_data.keys() if freq <= 1200})

    for config in configurations:
        avg_intervals = [config_avg_interval[config].get(freq, None) for freq in frequencies]

        # Remove None values for plotting
        freqs_plot = [freq for freq, avg in zip(frequencies, avg_intervals) if avg is not None]
        avg_intervals_plot = [avg for avg in avg_intervals if avg is not None]

        if not freqs_plot:
            continue

        plt.plot(freqs_plot, avg_intervals_plot, 
                 marker='o' if config in BLE_CONFIGS else 's',
                 color=CONFIG_COLORS.get(config, 'grey'),
                 linestyle='-', linewidth=2, markersize=12,
                 label=config)

    # Customize y-axis to be linear with reduced number of tick marks
    ax = plt.gca()
    ax.set_yscale('linear')

    # Define major ticks every 3 ms to reduce the number of y-ticks by two-thirds
    ax.yaxis.set_major_locator(MultipleLocator(3))  # Major ticks every 3 ms
    ax.yaxis.set_major_formatter(FormatStrFormatter('%.1f'))  # Format ticks to one decimal place

    # Remove minor ticks to further reduce clutter
    ax.minorticks_off()

    # Customize font sizes
    plt.xlabel('Sampling Frequency (Hz)', fontsize=26)
    plt.ylabel('Average Connection Interval (ms)', fontsize=26)
    plt.title('Average Connection Interval of "Attribute Value Changed" Events', fontsize=28)

    # Set x-axis ticks as actual sampling frequencies
    plt.xticks(frequencies, labels=[str(freq) for freq in frequencies], fontsize=24)
    plt.yticks(fontsize=24)

    # Add horizontal line at expected interval (7.5 ms)
    plt.axhline(EXPECTED_INTERVAL_MS, color='red', linestyle='dashed', linewidth=2, label='Expected Interval (7.5 ms)')

    # Adjust legend
    handles, labels = plt.gca().get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    # Adjust legend to be inside the plot in the upper left corner
    plt.legend(by_label.values(), by_label.keys(), fontsize=24, loc='upper left', frameon=True)


    # Add grid for better readability
    plt.grid(True, linestyle='--', alpha=0.7)

    plt.tight_layout()
    plt.savefig(output_path)
    plt.close()
    print(f"Average connection interval plot saved to {output_path}")

def main():
    """
    Main function to process BLE log files, calculate average connection intervals, and generate plots.
    """
    parser = argparse.ArgumentParser(description='Process BLE log files and plot average connection intervals.')
    parser.add_argument('input_folder', help='Path to the input folder containing all BLE log files.')
    parser.add_argument('output_folder', help='Path to the output folder for processed CSV files and plots.')
    parser.add_argument('--output_plot', default='average_connection_interval_plot.png', help='Filename for the average connection interval plot.')
    args = parser.parse_args()

    base_folder = args.input_folder
    output_folder = args.output_folder
    output_plot = args.output_plot

    # Ensure output folder exists
    os.makedirs(output_folder, exist_ok=True)

    print("Starting BLE Log Processing and Connection Interval Analysis.")

    try:
        # Step 1: Process Files
        processed_files = process_files(base_folder, output_folder)

        # Step 2: Calculate Average Connection Intervals
        interval_data = {}
        for freq, config, csv_path in processed_files:
            if os.path.exists(csv_path):
                try:
                    df = pd.read_csv(csv_path)
                    if 'timestamp' in df.columns and not df.empty:
                        # Convert 'timestamp' column to datetime
                        try:
                            df['timestamp'] = pd.to_datetime(df['timestamp'])
                        except Exception as e:
                            print(f"Failed to convert timestamps in {csv_path}: {e}")
                            continue

                        timestamps = df['timestamp'].tolist()
                        intervals_df = calculate_average_interval(timestamps, expected_interval_ms=EXPECTED_INTERVAL_MS)
                        interval_data[(freq, config)] = intervals_df
                    else:
                        print(f"'timestamp' column missing or CSV empty in {csv_path}. Skipping interval calculation.")
                except Exception as e:
                    print(f"Error reading CSV file {csv_path}: {e}")
            else:
                print(f"CSV file not found: {csv_path}. Skipping interval calculation for this file.")

        if not interval_data:
            print("No interval data available to plot.")
        else:
            # Step 3: Generate Plot
            plot_path = os.path.join(output_folder, output_plot)
            plot_average_interval(interval_data, plot_path)

        print("Connection Interval Analysis and Plotting Completed Successfully.")

    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    main()
