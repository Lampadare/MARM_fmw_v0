import struct
import csv
import os
import argparse
import glob
import numpy as np
import re
from datetime import datetime
import pandas as pd
import matplotlib.pyplot as plt
import logging
from multiprocessing import Pool, cpu_count

# Constants
MAX_CHANNELS = 16
ADC_SCALE_FACTOR = 0.195  # Typical scale factor RHD2000 in µV/bit
SNIPPET_DURATION = 10  # seconds (updated from 5)
MAX_ALLOWED_PACKET_LOSS = 100  # updated from 10

# Configure matplotlib style
plt.style.use('seaborn-darkgrid')

# Define color mapping for configurations with differentiation between SD and BLE within groups
CONFIG_COLORS = {
    # Intan Configurations
    'BLE+SD (Intan) SD': '#1f77b4',      # Dark Blue
    'BLE+SD (Intan) BLE': '#54d1ff',     # Light Blue
    'BLE (Intan)': '#2ca02c',            # Green
    'SD (Intan)': '#9467bd',             # Purple
    
    # Fakedata Configurations
    'BLE+SD (Fakedata) SD': '#ff7f0e',   # Dark Orange
    'BLE+SD (Fakedata) BLE': '#ffb347',  # Light Orange
}

# Define BLE configurations for marker style differentiation
BLE_CONFIGS = [
    'BLE+SD (Intan) BLE',
    'BLE+SD (Fakedata) BLE',
    'BLE (Intan)'
]

def setup_logging(output_folder):
    """
    Set up logging configuration.

    Args:
        output_folder (str): Path to save the log file.
    """
    log_file = os.path.join(output_folder, 'processing.log')
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s %(levelname)s:%(message)s',
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler()
        ]
    )

def decode_binary_files(input_folder, output_file):
    """
    Decode binary data files into a CSV file.

    Args:
        input_folder (str): Path to the folder containing binary files.
        output_file (str): Path to the output CSV file.
    """
    try:
        all_rows = []
        bin_files = sorted(
            glob.glob(os.path.join(input_folder, 'data_*.bin')),
            key=lambda x: int(re.search(r'data_(\d+).bin', x).group(1)) if re.search(r'data_(\d+).bin', x) else 0
        )

        base_timestamp_ms = 0  # Initialize in milliseconds
        last_timestamp_ms = 0

        first_timestamps = []

        for bin_file_path in bin_files:
            logging.info(f"Processing {bin_file_path}")
            with open(bin_file_path, 'rb') as bin_file:
                file_rows = 0
                while True:
                    binary_data = bin_file.read(36)
                    if not binary_data or len(binary_data) != 36:
                        break

                    channel_data = struct.unpack('<16h', binary_data[:32])
                    timestamp = struct.unpack('<I', binary_data[32:])[0]  # Milliseconds

                    # If current timestamp is less than last, assume timestamp reset
                    if timestamp < last_timestamp_ms:
                        base_timestamp_ms += last_timestamp_ms

                    timestamp_total_ms = timestamp + base_timestamp_ms
                    timestamp_seconds = timestamp_total_ms / 1000.0
                    last_timestamp_ms = timestamp_total_ms

                    channel_data = [value * ADC_SCALE_FACTOR for value in channel_data]

                    row = [timestamp_seconds] + channel_data
                    all_rows.append(row)
                    file_rows += 1

                    # Collect first few timestamps for logging
                    if len(first_timestamps) < 10:
                        first_timestamps.append(timestamp_seconds)

            logging.info(f"Processed {file_rows} rows from {bin_file_path}")

        if not all_rows:
            logging.warning(f"No data found in binary files within {input_folder}. Creating failed CSV.")
            create_failed_csv(output_file)
            return

        df = pd.DataFrame(all_rows, columns=['timestamp'] + [f'ch{i+1}' for i in range(MAX_CHANNELS)])

        # Log first few timestamps
        logging.info(f"First 10 timestamps in {input_folder}: {first_timestamps}")

        # Log first and last timestamps
        first_timestamp = df['timestamp'].min()
        last_timestamp = df['timestamp'].max()
        logging.info(f"Data range: {first_timestamp} to {last_timestamp} seconds")
        logging.info(f"Total duration: {last_timestamp - first_timestamp} seconds")
        logging.info(f"Total samples: {len(df)}")

        # Calculate snippet indices
        total_duration = last_timestamp - first_timestamp
        if total_duration < SNIPPET_DURATION:
            logging.warning(f"Total duration {total_duration} seconds is less than snippet duration {SNIPPET_DURATION} seconds. Using entire data.")
            snippet_df = df
        else:
            mid_time = first_timestamp + total_duration / 2
            start_time = mid_time - (SNIPPET_DURATION / 2)
            end_time = mid_time + (SNIPPET_DURATION / 2)
            snippet_df = df[(df['timestamp'] >= start_time) & (df['timestamp'] <= end_time)]

        snippet_duration = snippet_df['timestamp'].max() - snippet_df['timestamp'].min()
        logging.info(f"Snippet duration for {input_folder}: {snippet_duration} seconds")

        # Check if snippet_df is empty or has zero duration
        if snippet_df.empty:
            logging.warning(f"No data within the snippet duration for {input_folder}. Creating failed CSV.")
            create_failed_csv(output_file)
            return

        if snippet_duration <= 0:
            logging.warning(f"Snippet duration is non-positive ({snippet_duration} seconds) for {input_folder}. Creating failed CSV.")
            create_failed_csv(output_file)
            return

        # Log unique timestamps in snippet_df
        unique_timestamps = snippet_df['timestamp'].unique()
        logging.info(f"Unique timestamps in snippet_df: {unique_timestamps[:10]} ... Total unique timestamps: {len(unique_timestamps)}")

        snippet_df.to_csv(output_file, index=False)
        logging.info(f"Decoded binary files to {output_file} with snippet duration {SNIPPET_DURATION} seconds.")
    except Exception as e:
        logging.error(f"Error decoding binary files: {e}", exc_info=True)
        create_failed_csv(output_file)

def decode_ble_log(input_file, output_file):
    """
    Decode BLE log files into a CSV file.

    Args:
        input_file (str): Path to the BLE log file.
        output_file (str): Path to the output CSV file.
    """
    try:
        all_rows = []

        with open(input_file, 'r') as infile:
            # Initialize CSV writer
            with open(output_file, 'w', newline='') as outfile:
                csv_writer = csv.writer(outfile)
                header = ['timestamp'] + [f'ch{i+1}' for i in range(MAX_CHANNELS)]
                csv_writer.writerow(header)

                # Regex pattern to capture BLE logger data
                pattern = r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z).*handle: 0x12, value \(0x\): (.*)'

                for line in infile:
                    match = re.search(pattern, line)
                    if match:
                        timestamp_str, data = match.groups()

                        # Parse BLE logger's timestamp (not used in output CSV)
                        # timestamp_dt = datetime.strptime(timestamp_str, '%Y-%m-%dT%H:%M:%S.%fZ')
                        # timestamp_logger = timestamp_dt.timestamp()  # Convert to seconds since epoch
                        # Not utilized in this implementation as per working function

                        # Clean up the data (removing hyphens)
                        data_clean = data.replace('-', '')

                        # Convert hex string to bytes
                        data_bytes = bytes.fromhex(data_clean)

                        # Ensure we have exactly 36 bytes (32 for channels + 4 for timestamp)
                        if len(data_bytes) != 36:
                            logging.warning(f"Invalid data length: {len(data_bytes)} in line: {line.strip()}")
                            continue

                        # Unpack 16 signed short integers (each is 2 bytes) from the first 32 bytes
                        channel_data = struct.unpack('<16h', data_bytes[:32])

                        # Apply scaling factor to convert to µV
                        channel_data = [value * ADC_SCALE_FACTOR for value in channel_data]

                        # Extract the actual timestamp from the last 4 bytes of the data
                        ble_timestamp = struct.unpack('<I', data_bytes[32:])[0]
                        ble_timestamp_seconds = ble_timestamp / 1000.0  # Convert to seconds

                        # Create the row with the timestamp and channel data
                        row = [ble_timestamp_seconds] + channel_data
                        all_rows.append(row)

        if not all_rows:
            logging.warning(f"No data found in BLE log file {input_file}. Creating failed CSV.")
            create_failed_csv(output_file)
            return

        df = pd.DataFrame(all_rows, columns=['timestamp'] + [f'ch{i+1}' for i in range(MAX_CHANNELS)])

        # Calculate snippet indices
        total_duration = df['timestamp'].max() - df['timestamp'].min()
        if total_duration < SNIPPET_DURATION:
            logging.warning(f"Total duration {total_duration} seconds is less than snippet duration. Using entire data.")
            snippet_df = df
        else:
            mid_time = df['timestamp'].min() + total_duration / 2
            start_time = mid_time - (SNIPPET_DURATION / 2)
            end_time = mid_time + (SNIPPET_DURATION / 2)
            snippet_df = df[(df['timestamp'] >= start_time) & (df['timestamp'] <= end_time)]

        snippet_duration = snippet_df['timestamp'].max() - snippet_df['timestamp'].min()
        logging.info(f"Snippet duration for {input_file}: {snippet_duration} seconds")

        if snippet_df.empty:
            logging.warning(f"No data within the snippet duration for {input_file}. Creating failed CSV.")
            create_failed_csv(output_file)
            return

        # Write all rows to CSV at once (better for performance)
        snippet_df.to_csv(output_file, index=False)
        logging.info(f"Processed BLE file: {input_file}")
    except Exception as e:
        logging.error(f"Error processing BLE file {input_file}: {e}", exc_info=True)
        create_failed_csv(output_file)

def create_failed_csv(output_path):
    """
    Create a CSV file indicating a failed processing case.

    Args:
        output_path (str): Path to the failed CSV file.
    """
    try:
        with open(output_path, 'w', newline='') as csvfile:
            writer = csv.writer(csvfile)
            writer.writerow(['status'])
            writer.writerow(['failed'])
        logging.info(f"Created failed CSV: {output_path}")
    except Exception as e:
        logging.error(f"Error creating failed CSV {output_path}: {e}", exc_info=True)

def process_files(base_folder, output_folder):
    """
    Process all files based on the provided file mapping.

    Args:
        base_folder (str): Path to the base folder containing all data files.
        output_folder (str): Path to the output folder for processed CSV files.

    Returns:
        tuple: Summary of processing results.
    """
    # Define only the 6 existing configurations
    file_mapping = {
        100: {
            'BLE+SD (Intan) SD': 'session_1',
            'BLE+SD (Intan) BLE': 'b1.txt',
            'BLE (Intan)': 'b10.txt',
            'SD (Intan)': 'session_17',
            'BLE+SD (Fakedata) SD': 'f_session_1',
            'BLE+SD (Fakedata) BLE': 'fb1.txt'
        },
        250: {
            'BLE+SD (Intan) SD': 'session_2',
            'BLE+SD (Intan) BLE': 'b2.txt',
            'BLE (Intan)': 'b11.txt',
            'SD (Intan)': 'session_18',
            'BLE+SD (Fakedata) SD': 'f_session_2',
            'BLE+SD (Fakedata) BLE': 'fb2.txt'
        },
        500: {
            'BLE+SD (Intan) SD': 'session_3',
            'BLE+SD (Intan) BLE': 'b3.txt',
            'BLE (Intan)': 'b12.txt',
            'SD (Intan)': 'session_19',
            'BLE+SD (Fakedata) SD': 'f_session_3',
            'BLE+SD (Fakedata) BLE': 'fb3.txt'
        },
        750: {
            'BLE+SD (Intan) SD': 'session_4',
            'BLE+SD (Intan) BLE': 'b4.txt',
            'BLE (Intan)': 'b13.txt',
            'SD (Intan)': 'session_20',
            'BLE+SD (Fakedata) SD': 'f_session_4',
            'BLE+SD (Fakedata) BLE': 'fb4.txt'
        },
        1000: {
            'BLE+SD (Intan) SD': 'session_5',
            'BLE+SD (Intan) BLE': 'b5.txt',
            'BLE (Intan)': 'b14.txt',
            'SD (Intan)': 'session_21',
            'BLE+SD (Fakedata) SD': 'f_session_5',
            'BLE+SD (Fakedata) BLE': 'fb5.txt'
        },
        1200: {
            'BLE+SD (Intan) SD': 'session_6',
            'BLE+SD (Intan) BLE': 'b6.txt',
            'BLE (Intan)': 'b15.txt',
            'SD (Intan)': 'session_22',
            'BLE+SD (Fakedata) SD': 'f_session_6',
            'BLE+SD (Fakedata) BLE': 'fb6.txt'
        },
        1500: {
            'BLE+SD (Intan) SD': 'session_7',
            'BLE+SD (Intan) BLE': 'b7.txt',
            'BLE (Intan)': 'b16.txt',
            'SD (Intan)': 'session_23',
            'BLE+SD (Fakedata) SD': 'f_session_7',
            'BLE+SD (Fakedata) BLE': 'fb7.txt'
        },
        2000: {
            'BLE+SD (Intan) SD': 'session_8',
            'BLE+SD (Intan) BLE': 'b8.txt',
            'BLE (Intan)': 'b17.txt',
            'SD (Intan)': 'session_24',
            'BLE+SD (Fakedata) SD': 'f_session_8',
            'BLE+SD (Fakedata) BLE': 'fb8.txt'
        },
        2500: {
            'BLE+SD (Intan) SD': 'session_9',
            'BLE+SD (Intan) BLE': 'b9.txt',
            'BLE (Intan)': 'b18.txt',
            'SD (Intan)': 'session_25',
            'BLE+SD (Fakedata) SD': 'f_session_9',
            'BLE+SD (Fakedata) BLE': 'fb9.txt'
        }
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

    total_expected_files = sum(len(files) for files in file_mapping.values())
    total_expected_failed = len(failed_cases)
    files_processed = 0
    expected_failed_created = 0
    unexpected_failed_files = 0

    for freq, files in file_mapping.items():
        for config, filename in files.items():
            input_path = os.path.join(base_folder, filename)
            sanitized_config = config.replace(' ', '_').replace('(', '').replace(')', '').replace('+', 'plus')
            output_path = os.path.join(output_folder, f"{freq}_{sanitized_config}.csv")

            if (freq, config) in failed_cases:
                create_failed_csv(output_path)
                expected_failed_created += 1
                logging.info(f"Created expected failed CSV for {freq} Hz, {config}")
            elif os.path.exists(input_path):
                try:
                    if os.path.isdir(input_path):
                        # This is an SD folder
                        decode_binary_files(input_path, output_path)
                        files_processed += 1
                        logging.info(f"Processed SD folder: {input_path}")
                    elif os.path.isfile(input_path):
                        # This is a BLE file
                        decode_ble_log(input_path, output_path)
                        files_processed += 1
                        logging.info(f"Processed BLE file: {input_path}")
                    else:
                        logging.warning(f"Unknown file type: {input_path}")
                        create_failed_csv(output_path)
                        unexpected_failed_files += 1
                except Exception as e:
                    logging.error(f"Error processing {input_path}: {e}", exc_info=True)
                    create_failed_csv(output_path)
                    unexpected_failed_files += 1
            else:
                logging.warning(f"File or folder not found: {input_path}")
                create_failed_csv(output_path)
                unexpected_failed_files += 1

    logging.info(f"\nProcessing Summary:")
    logging.info(f"Total files processed: {files_processed} out of {total_expected_files} expected files")
    logging.info(f"Expected failed files created: {expected_failed_created} out of {total_expected_failed}")
    logging.info(f"Unexpected failed files created: {unexpected_failed_files}")
    logging.info(f"Total failed files created: {expected_failed_created + unexpected_failed_files}")

    return files_processed, total_expected_files, expected_failed_created, total_expected_failed, unexpected_failed_files

def calculate_actual_packets(csv_file):
    """
    Calculate the total number of packets (samples) from a CSV file.

    Args:
        csv_file (str): Path to the CSV file.

    Returns:
        int: Total number of packets (samples).
    """
    try:
        df = pd.read_csv(csv_file)
        if 'timestamp' not in df.columns:
            logging.warning(f"'timestamp' column missing in {csv_file}. Skipping packet count.")
            return None  # Indicate that packet count was not calculated

        total_packets = len(df)
        return total_packets
    except FileNotFoundError:
        logging.error(f"File not found: {csv_file}")
        return None
    except Exception as e:
        logging.error(f"Error calculating packet count for {csv_file}: {e}", exc_info=True)
        return None

def calculate_packet_loss(csv_file, expected_samples):
    """
    Calculate the number of packets lost from a CSV file.

    Args:
        csv_file (str): Path to the CSV file.
        expected_samples (int): Expected number of samples based on sampling frequency and duration.

    Returns:
        int: Number of packets lost.
    """
    try:
        df = pd.read_csv(csv_file)
        if 'timestamp' not in df.columns:
            logging.warning(f"'timestamp' column missing in {csv_file}. Assuming all packets lost.")
            return expected_samples  # Assume all packets lost if timestamp missing

        actual_samples = len(df)
        packets_lost = expected_samples - actual_samples
        return packets_lost if packets_lost >= 0 else 0
    except FileNotFoundError:
        logging.error(f"File not found: {csv_file}")
        return expected_samples  # Assume all packets lost if file missing
    except Exception as e:
        logging.error(f"Error calculating packet loss for {csv_file}: {e}", exc_info=True)
        return expected_samples  # Conservative approach

def analyze_stability(packet_loss_data, max_allowed_packet_loss=MAX_ALLOWED_PACKET_LOSS):
    """
    Determine the highest stable sampling frequency for each configuration.

    Args:
        packet_loss_data (dict): Dictionary containing packet loss data.
        max_allowed_packet_loss (int): Maximum allowed packets lost.

    Returns:
        dict: Highest stable sampling frequency for each configuration.
    """
    stability = {}
    try:
        for config in packet_loss_data:
            stable_freqs = [sf for sf, loss in packet_loss_data[config].items() if loss <= max_allowed_packet_loss]
            stability[config] = max(stable_freqs) if stable_freqs else None
            if stability[config]:
                logging.info(f"Highest stable frequency for {config}: {stability[config]} Hz")
            else:
                logging.info(f"No stable frequency found for {config}")
    except Exception as e:
        logging.error(f"Error analyzing stability: {e}", exc_info=True)
    return stability

def calculate_efficiency(throughput_data):
    """
    Calculate throughput efficiency for each configuration across sampling frequencies.

    Args:
        throughput_data (dict): Dictionary containing throughput data.

    Returns:
        dict: Efficiency ratios.
    """
    efficiency = {}
    try:
        for config in throughput_data:
            efficiency[config] = {}
            for sf, data in throughput_data[config].items():
                expected = data['expected']
                actual = data['actual']
                ratio = (actual / expected) if expected > 0 else 0
                efficiency[config][sf] = ratio
                logging.info(f"Config: {config}, Sampling Frequency: {sf} Hz, Expected: {expected}, Actual: {actual}, Efficiency: {ratio*100:.2f}%")
        logging.info("Calculated throughput efficiency.")
    except Exception as e:
        logging.error(f"Error calculating efficiency: {e}", exc_info=True)
    return efficiency

def plot_throughput_comparison(data, output_folder):
    """
    Plot throughput comparison for different configurations.

    Args:
        data (dict): Dictionary containing throughput data.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        # Plot the expected throughput once as a light gray dashed line
        sampling_frequencies = sorted(next(iter(data['throughput'].values())).keys())
        expected_throughput = [sf * SNIPPET_DURATION for sf in sampling_frequencies]
        plt.plot(sampling_frequencies, expected_throughput, linestyle='--', color='lightgray', label='Expected Throughput')

        # Plot actual throughput for each configuration
        for config in data['throughput']:
            sf_sorted = sorted(data['throughput'][config].keys())
            actual = [data['throughput'][config][sf]['actual'] for sf in sf_sorted]
            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, actual, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=f'{config} Actual')

        plt.xlabel('Sampling Frequency (Hz)', fontsize=18)
        plt.ylabel('Throughput (Samples/second)', fontsize=18)
        plt.title('Throughput Comparison', fontsize=20)
        plt.legend(fontsize=16)
        plt.xticks(fontsize=16)
        plt.yticks(fontsize=16)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'throughput_comparison.png'))
        plt.close()
        logging.info("Generated throughput comparison plot.")
    except Exception as e:
        logging.error(f"Error plotting throughput comparison: {e}", exc_info=True)

def plot_packet_loss(data, output_folder):
    """
    Plot packet loss for different configurations.

    Args:
        data (dict): Dictionary containing packet loss data.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        for config in data['packet_loss']:
            sf_sorted = sorted(data['packet_loss'][config].keys())
            packets_lost = [data['packet_loss'][config][sf] for sf in sf_sorted]
            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, packets_lost, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=18)
        plt.ylabel('Number of Packets Lost', fontsize=18)
        plt.title('Packet Loss Analysis', fontsize=20)
        plt.legend(fontsize=16)
        plt.xticks(fontsize=16)
        plt.yticks(fontsize=16)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'packet_loss_analysis.png'))
        plt.close()
        logging.info("Generated packet loss analysis plot.")
    except Exception as e:
        logging.error(f"Error plotting packet loss: {e}", exc_info=True)

def plot_combined_throughput_packet_loss(data, output_folder):
    """
    Plot combined throughput and packet loss for different configurations.

    Args:
        data (dict): Dictionary containing throughput and packet loss data.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        for config in data['throughput']:
            sf_sorted = sorted(data['throughput'][config].keys())
            throughput = [data['throughput'][config][sf]['actual'] / 10 for sf in sf_sorted]
            packet_loss = [data['packet_loss'][config][sf] / 10 for sf in sf_sorted]

            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, throughput, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=f'{config} Throughput')
            plt.plot(sf_sorted, packet_loss, marker='x', linestyle='--', color=CONFIG_COLORS.get(config, 'grey'), label=f'{config} Packet Loss')

        plt.xlabel('Sampling Frequency (Hz)', fontsize=18)
        plt.ylabel('Values', fontsize=18)
        plt.title('Combined Throughput and Packet Loss', fontsize=20)
        plt.legend(fontsize=16)
        plt.xticks(fontsize=16)
        plt.yticks(fontsize=16)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'combined_throughput_packet_loss.png'))
        plt.close()
        logging.info("Generated combined throughput and packet loss plot.")
    except Exception as e:
        logging.error(f"Error plotting combined throughput and packet loss: {e}", exc_info=True)

def plot_intan_vs_fakedata(throughput_data, output_folder):
    """
    Plot Intan vs. Fake Data Throughput comparison.

    Args:
        throughput_data (dict): Dictionary containing throughput data.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        # Separate Intan and Fakedata configurations
        intan_configs = [config for config in throughput_data if 'Intan' in config]
        fakedata_configs = [config for config in throughput_data if 'Fakedata' in config]

        for config in intan_configs + fakedata_configs:
            sf_sorted = sorted(throughput_data[config].keys())
            throughput = [throughput_data[config][sf]['actual'] for sf in sf_sorted]
            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, throughput, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=18)
        plt.ylabel('Throughput (Samples/second)', fontsize=18)
        plt.title('Intan vs. Fake Data Throughput Comparison', fontsize=20)
        plt.legend(fontsize=16)
        plt.xticks(fontsize=16)
        plt.yticks(fontsize=16)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'intan_vs_fakedata_throughput.png'))
        plt.close()
        logging.info("Generated Intan vs. Fake Data throughput comparison plot.")
    except Exception as e:
        logging.error(f"Error plotting Intan vs. Fake Data throughput: {e}", exc_info=True)

def plot_system_stability(stability_data, output_folder):
    """
    Plot system stability analysis.

    Args:
        stability_data (dict): Dictionary containing stability analysis results.
        output_folder (str): Path to save the plot.
    """
    try:
        # Define Intan configurations to include
        INTAN_CONFIGS = [
            'BLE+SD (Intan) SD',
            'BLE+SD (Intan) BLE',
            'BLE (Intan)',
            'SD (Intan)'
        ]

        configurations = [config for config in stability_data.keys() if config in INTAN_CONFIGS]
        frequencies = [stability_data[config] if stability_data[config] else 0 for config in configurations]

        plt.figure(figsize=(12, 8))
        bars = plt.bar(configurations, frequencies, color=[CONFIG_COLORS.get(config, 'grey') for config in configurations])

        plt.xlabel('Configuration', fontsize=24)
        plt.ylabel('Highest Stable Sampling Frequency (Hz)', fontsize=24)
        plt.title('System Stability Analysis', fontsize=26)
        max_freq = max(frequencies) if frequencies else 1
        plt.ylim(0, max_freq * 1.2)
        plt.grid(axis='y')

        # Annotate bars with frequency values
        for bar, freq in zip(bars, frequencies):
            height = bar.get_height()
            annotation = f'{freq} Hz' if freq > 0 else 'N/A'
            plt.text(bar.get_x() + bar.get_width() / 2, height, annotation, ha='center', va='bottom', fontsize=16)

        plt.xticks(rotation=45, ha='right', fontsize=22)
        plt.yticks(fontsize=22)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'system_stability_analysis.png'))
        plt.close()
        logging.info("Generated system stability analysis plot.")
    except Exception as e:
        logging.error(f"Error plotting system stability analysis: {e}", exc_info=True)

def plot_throughput_efficiency(efficiency_data, output_folder):
    """
    Plot throughput efficiency for different configurations.

    Args:
        efficiency_data (dict): Dictionary containing efficiency ratios.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        for config in efficiency_data:
            sf_sorted = sorted(efficiency_data[config].keys())
            efficiency = [efficiency_data[config][sf] for sf in sf_sorted]
            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, efficiency, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=24)
        plt.ylabel('Throughput Efficiency (Actual / Expected)', fontsize=24)
        plt.title('Throughput Efficiency Comparison', fontsize=26)
        plt.legend(fontsize=22)
        plt.xticks(fontsize=22)
        plt.yticks(fontsize=22)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'throughput_efficiency.png'))
        plt.close()
        logging.info("Generated throughput efficiency plot.")
    except Exception as e:
        logging.error(f"Error plotting throughput efficiency: {e}", exc_info=True)

def plot_overall_performance(throughput_data, output_folder):
    """
    Plot overall system performance comparison.

    Args:
        throughput_data (dict): Dictionary containing throughput data.
        output_folder (str): Path to save the plot.
    """
    try:
        plt.figure(figsize=(12, 8))

        for config in throughput_data:
            sf_sorted = sorted(throughput_data[config].keys())
            throughput = [throughput_data[config][sf]['actual'] for sf in sf_sorted]
            marker_style = 's' if config in BLE_CONFIGS else 'o'
            plt.plot(sf_sorted, throughput, marker=marker_style, color=CONFIG_COLORS.get(config, 'grey'), label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=18)
        plt.ylabel('Throughput (Samples/second)', fontsize=18)
        plt.title('Overall System Performance Comparison', fontsize=20)
        plt.legend(fontsize=16)
        plt.xticks(fontsize=16)
        plt.yticks(fontsize=16)
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'overall_system_performance.png'))
        plt.close()
        logging.info("Generated overall system performance comparison plot.")
    except Exception as e:
        logging.error(f"Error plotting overall system performance: {e}", exc_info=True)

def calculate_average_time(csv_file):
    """
    Calculate the average time between samples in a CSV file.

    Args:
        csv_file (str): Path to the CSV file.

    Returns:
        float: Average time between samples in seconds.
    """
    try:
        df = pd.read_csv(csv_file)
        if 'timestamp' not in df.columns or len(df) < 2:
            logging.warning(f"Cannot calculate average time for {csv_file} due to insufficient data.")
            return None

        time_diffs = df['timestamp'].diff().dropna()
        average_time = time_diffs.mean()
        return average_time
    except FileNotFoundError:
        logging.error(f"File not found: {csv_file}")
        return None
    except Exception as e:
        logging.error(f"Error calculating average time for {csv_file}: {e}", exc_info=True)
        return None

def process_configuration_updated(args_tuple):
    """
    Helper function for multiprocessing to process a single configuration.

    Args:
        args_tuple (tuple): Contains config, parent_config, sf, output_folder, expected_packets.

    Returns:
        tuple: (parent_config, sf, actual_packets, packet_loss, average_sample_time)
    """
    config, parent_config, sf, output_folder, expected_packets = args_tuple
    sanitized_config = config.replace(' ', '_').replace('(', '').replace(')', '').replace('+', 'plus')
    csv_file = os.path.join(output_folder, f"{sf}_{sanitized_config}.csv")
    actual_packets = calculate_actual_packets(csv_file)
    packet_loss = calculate_packet_loss(csv_file, expected_packets[sf])
    average_sample_time = calculate_average_time(csv_file)  # New line added
    return (parent_config, sf, actual_packets, packet_loss, average_sample_time)  # Modified to include average_sample_time


def main():
    """
    Main function to process neural data files, perform analysis, and generate plots.
    """
    parser = argparse.ArgumentParser(description='Process neural data files and generate analysis plots.')
    parser.add_argument('base_folder', help='Path to the base folder containing all data files')
    parser.add_argument('output_folder', help='Path to the output folder for processed CSV files and plots')
    args = parser.parse_args()

    os.makedirs(args.output_folder, exist_ok=True)
    setup_logging(args.output_folder)
    logging.info("Starting neural data processing.")

    try:
        # Step 1: Process files
        files_processed, total_expected, expected_failed_created, total_expected_failed, unexpected_failed_files = process_files(
            args.base_folder, args.output_folder
        )

        logging.info("\nFinal Processing Summary:")
        logging.info(f"Total files processed: {files_processed} out of {total_expected} expected files")
        logging.info(f"Expected failed files created: {expected_failed_created} out of {total_expected_failed}")
        logging.info(f"Unexpected failed files created: {unexpected_failed_files}")
        logging.info(f"Total failed files created: {expected_failed_created + unexpected_failed_files}")

        # Step 2: Analyze Data
        logging.info("Starting data analysis...")

        # Define sampling frequencies based on file_mapping keys
        sampling_frequencies = [100, 250, 500, 750, 1000, 1200, 1500, 2000, 2500]

        # Calculate expected packets based on SNIPPET_DURATION and sampling frequency
        expected_packets = {sf: SNIPPET_DURATION * sf for sf in sampling_frequencies}
        for sf in sampling_frequencies:
            logging.info(f"Expected packets for {sf} Hz: {expected_packets[sf]}")

        # Prepare data structures with only the 6 configurations
        throughput_data = {
            'BLE+SD (Intan) SD': {},
            'BLE+SD (Intan) BLE': {},
            'BLE+SD (Fakedata) SD': {},
            'BLE+SD (Fakedata) BLE': {},
            'BLE (Intan)': {},
            'SD (Intan)': {}
        }
        packet_loss_data = {
            'BLE+SD (Intan) SD': {},
            'BLE+SD (Intan) BLE': {},
            'BLE+SD (Fakedata) SD': {},
            'BLE+SD (Fakedata) BLE': {},
            'BLE (Intan)': {},
            'SD (Intan)': {}
        }
        average_time_data = {  # New data structure added
            'BLE+SD (Intan) SD': {},
            'BLE+SD (Intan) BLE': {},
            'BLE+SD (Fakedata) SD': {},
            'BLE+SD (Fakedata) BLE': {},
            'BLE (Intan)': {},
            'SD (Intan)': {}
        }

        # Prepare arguments for multiprocessing
        configs = [
            'BLE+SD (Intan) SD',
            'BLE+SD (Intan) BLE',
            'BLE+SD (Fakedata) SD',
            'BLE+SD (Fakedata) BLE',
            'BLE (Intan)',
            'SD (Intan)'
        ]
        args_list = []
        for config in configs:
            for sf in sampling_frequencies:
                args_list.append((config, config, sf, args.output_folder, expected_packets))

        # Use multiprocessing to speed up calculations
        with Pool(processes=cpu_count()) as pool:
            results = pool.map(process_configuration_updated, args_list)

        for result in results:
            if len(result) == 5:
                parent_config, sf, actual_packets, packet_loss, average_sample_time = result
                if parent_config in throughput_data:
                    if actual_packets is not None:
                        throughput_data[parent_config][sf] = {
                            'expected': expected_packets[sf],
                            'actual': actual_packets
                        }
                    else:
                        throughput_data[parent_config][sf] = {
                            'expected': expected_packets[sf],
                            'actual': 0  # No actual packets available
                        }

                    if packet_loss is not None:
                        packet_loss_data[parent_config][sf] = packet_loss
                    else:
                        packet_loss_data[parent_config][sf] = expected_packets[sf]  # Assume all packets lost

                    if average_sample_time is not None:
                        average_time_data[parent_config][sf] = average_sample_time
                    else:
                        average_time_data[parent_config][sf] = None  # No data available

        # Analyze Stability
        stability = analyze_stability(packet_loss_data, max_allowed_packet_loss=MAX_ALLOWED_PACKET_LOSS)

        # Calculate Efficiency
        efficiency = calculate_efficiency(throughput_data)

        # Compile all data into a single dictionary
        data = {
            'throughput': throughput_data,
            'packet_loss': packet_loss_data,
            'stability': stability,
            'efficiency': efficiency
        }

        logging.info("Data analysis completed.")

        # Step 3: Print Average Time Between Samples vs Expected
        logging.info("\nAverage Time Between Samples vs Expected:")
        for config in average_time_data:
            for sf in sorted(average_time_data[config]):
                avg_time = average_time_data[config][sf]
                expected_time = 1.0 / sf
                if avg_time is not None:
                    deviation = avg_time - expected_time
                    logging.info(f"Config: {config}, Sampling Frequency: {sf} Hz, "
                                 f"Average Time: {avg_time:.6f} s, Expected Time: {expected_time:.6f} s, "
                                 f"Deviation: {deviation:.6f} s")
                else:
                    logging.info(f"Config: {config}, Sampling Frequency: {sf} Hz, "
                                 f"Average Time: N/A, Expected Time: {expected_time:.6f} s")

        # Step 4: Generate Plots
        logging.info("Starting plot generation...")

        plot_throughput_comparison(data, args.output_folder)
        plot_packet_loss(data, args.output_folder)
        plot_combined_throughput_packet_loss(data, args.output_folder)
        plot_intan_vs_fakedata(data['throughput'], args.output_folder)
        plot_system_stability(stability_data=stability, output_folder=args.output_folder)  # Corrected parameter
        plot_throughput_efficiency(efficiency_data=efficiency, output_folder=args.output_folder)  # Corrected parameter
        plot_overall_performance(throughput_data=data['throughput'], output_folder=args.output_folder)  # Corrected parameter

        logging.info("Plot generation completed.")
        logging.info("All processing and plotting completed successfully.")

    except Exception as e:
        logging.error(f"An unexpected error occurred: {e}", exc_info=True)


if __name__ == "__main__":
    main()
