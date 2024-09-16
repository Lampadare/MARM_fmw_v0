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
ADC_SCALE_FACTOR = 0.195  # typical scale factor RHD2000 in µV/bit
SNIPPET_DURATION = 5  # seconds

# Configure matplotlib style
plt.style.use('seaborn-darkgrid')

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

        for bin_file_path in bin_files:
            logging.info(f"Processing {bin_file_path}")
            with open(bin_file_path, 'rb') as bin_file:
                while True:
                    binary_data = bin_file.read(36)
                    if not binary_data or len(binary_data) != 36:
                        break

                    channel_data = struct.unpack('<16h', binary_data[:32])
                    timestamp = struct.unpack('<I', binary_data[32:])[0]

                    # Assuming timestamp is in milliseconds; convert to seconds
                    timestamp_seconds = timestamp / 1000.0

                    channel_data = [value * ADC_SCALE_FACTOR for value in channel_data]

                    row = [timestamp_seconds] + channel_data
                    all_rows.append(row)

        if not all_rows:
            logging.warning(f"No data found in binary files within {input_folder}. Creating failed CSV.")
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
            pattern = r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z).*handle: 0x12, value \(0x\): (.*)'

            for line in infile:
                match = re.search(pattern, line)
                if match:
                    timestamp_str, data = match.groups()
                    # Parse BLE logger's timestamp if needed
                    # timestamp_dt = datetime.strptime(timestamp_str, '%Y-%m-%dT%H:%M:%S.%fZ')
                    # timestamp_logger = timestamp_dt.timestamp()  # Convert to seconds since epoch
                    # However, based on your working code, we'll use the binary timestamp

                    data_clean = data.replace('-', '')

                    # Convert hex string to bytes
                    data_bytes = bytes.fromhex(data_clean)

                    if len(data_bytes) < 36:
                        logging.warning(f"Incomplete data packet in {input_file}: {data}")
                        continue

                    # Unpack 16 signed short integers (2 bytes each)
                    channel_data = struct.unpack('<16h', data_bytes[:32])

                    # Convert to microvolts and apply scaling factor
                    channel_data = [value * ADC_SCALE_FACTOR for value in channel_data]

                    # Extract timestamp from the last 4 bytes (binary timestamp)
                    ble_timestamp = struct.unpack('<I', data_bytes[32:])[0]

                    row = [ble_timestamp] + channel_data
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

        snippet_df.to_csv(output_file, index=False)
        logging.info(f"Processed BLE file: {input_file} to {output_file} with snippet duration {SNIPPET_DURATION} seconds.")
    except Exception as e:
        logging.error(f"Error decoding BLE log {input_file}: {e}", exc_info=True)
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

def calculate_throughput(csv_file):
    """
    Calculate the throughput (samples per second) from a CSV file.

    Args:
        csv_file (str): Path to the CSV file.

    Returns:
        float: Throughput in samples/second.
    """
    try:
        df = pd.read_csv(csv_file)
        if 'timestamp' not in df.columns:
            logging.warning(f"'timestamp' column missing in {csv_file}. Skipping throughput calculation.")
            return None  # Indicate that throughput was not calculated

        total_samples = len(df)
        duration = df['timestamp'].max() - df['timestamp'].min()
        throughput = total_samples / duration if duration > 0 else 0
        return throughput
    except FileNotFoundError:
        logging.error(f"File not found: {csv_file}")
        return None
    except Exception as e:
        logging.error(f"Error calculating throughput for {csv_file}: {e}", exc_info=True)
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

def analyze_stability(packet_loss_data, max_allowed_packet_loss=10):
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

        for config in data['throughput']:
            sf_sorted = sorted(data['throughput'][config].keys())
            expected = [data['throughput'][config][sf]['expected'] for sf in sf_sorted]
            actual = [data['throughput'][config][sf]['actual'] for sf in sf_sorted]
            plt.plot(sf_sorted, expected, linestyle='--', label=f'{config} Expected')
            plt.plot(sf_sorted, actual, marker='o', label=f'{config} Actual')

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Throughput (Samples/second)', fontsize=14)
        plt.title('Throughput Comparison', fontsize=16)
        plt.legend()
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
            plt.plot(sf_sorted, packets_lost, marker='o', label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Number of Packets Lost', fontsize=14)
        plt.title('Packet Loss Analysis', fontsize=16)
        plt.legend()
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
            throughput = [data['throughput'][config][sf]['actual'] for sf in sf_sorted]
            packet_loss = [data['packet_loss'][config][sf] for sf in sf_sorted]

            plt.plot(sf_sorted, throughput, marker='o', label=f'{config} Throughput')
            plt.plot(sf_sorted, packet_loss, marker='x', linestyle='--', label=f'{config} Packet Loss')

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Values', fontsize=14)
        plt.title('Combined Throughput and Packet Loss', fontsize=16)
        plt.legend()
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
            plt.plot(sf_sorted, throughput, marker='o', label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Throughput (Samples/second)', fontsize=14)
        plt.title('Intan vs. Fake Data Throughput Comparison', fontsize=16)
        plt.legend()
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
        configurations = list(stability_data.keys())
        frequencies = [stability_data[config] if stability_data[config] else 0 for config in configurations]

        plt.figure(figsize=(12, 8))
        bars = plt.bar(configurations, frequencies, color=['blue', 'green', 'red', 'purple', 'orange', 'cyan'])

        plt.xlabel('Configuration', fontsize=14)
        plt.ylabel('Highest Stable Sampling Frequency (Hz)', fontsize=14)
        plt.title('System Stability Analysis', fontsize=16)
        plt.ylim(0, max(frequencies) * 1.2 if frequencies else 1)
        plt.grid(axis='y')

        # Annotate bars with frequency values
        for bar, freq in zip(bars, frequencies):
            height = bar.get_height()
            plt.text(bar.get_x() + bar.get_width() / 2, height, f'{freq} Hz', ha='center', va='bottom', fontsize=12)

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
            plt.plot(sf_sorted, efficiency, marker='o', label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Throughput Efficiency (Actual / Expected)', fontsize=14)
        plt.title('Throughput Efficiency Comparison', fontsize=16)
        plt.legend()
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
            plt.plot(sf_sorted, throughput, marker='o', label=config)

        plt.xlabel('Sampling Frequency (Hz)', fontsize=14)
        plt.ylabel('Throughput (Samples/second)', fontsize=14)
        plt.title('Overall System Performance Comparison', fontsize=16)
        plt.legend()
        plt.grid(True)
        plt.tight_layout()
        plt.savefig(os.path.join(output_folder, 'overall_system_performance.png'))
        plt.close()
        logging.info("Generated overall system performance comparison plot.")
    except Exception as e:
        logging.error(f"Error plotting overall system performance: {e}", exc_info=True)

def process_configuration_updated(args_tuple):
    """
    Helper function for multiprocessing to process a single configuration.

    Args:
        args_tuple (tuple): Contains config, parent_config, sf, output_folder, expected_packets.

    Returns:
        tuple: (parent_config, sf, throughput, packet_loss)
    """
    config, parent_config, sf, output_folder, expected_packets = args_tuple
    sanitized_config = config.replace(' ', '_').replace('(', '').replace(')', '').replace('+', 'plus')
    csv_file = os.path.join(output_folder, f"{sf}_{sanitized_config}.csv")
    throughput = calculate_throughput(csv_file)
    packet_loss = calculate_packet_loss(csv_file, expected_packets[sf])
    return (parent_config, sf, throughput, packet_loss)

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
        files_processed, total_expected, expected_failed_created, total_failed_expected, unexpected_failed_files = process_files(args.base_folder, args.output_folder)

        logging.info("\nFinal Processing Summary:")
        logging.info(f"Total files processed: {files_processed} out of {total_expected} expected files")
        logging.info(f"Expected failed files created: {expected_failed_created} out of {total_failed_expected}")
        logging.info(f"Unexpected failed files created: {unexpected_failed_files}")
        logging.info(f"Total failed files created: {expected_failed_created + unexpected_failed_files}")

        # Step 2: Analyze Data
        logging.info("Starting data analysis...")

        # Define sampling frequencies based on file_mapping keys
        sampling_frequencies = [100, 250, 500, 750, 1000, 1200, 1500, 2000, 2500]

        # Calculate expected packets based on sampling frequency and duration
        # Find duration from any successful CSV for each frequency
        expected_packets = {}
        for sf in sampling_frequencies:
            # Attempt to find a successful CSV for this frequency
            # Check all 6 configurations
            sample_csvs = [
                os.path.join(args.output_folder, f"{sf}_BLEplusSD_Intan_SD.csv"),
                os.path.join(args.output_folder, f"{sf}_BLEplusSD_Intan_BLE.csv"),
                os.path.join(args.output_folder, f"{sf}_BLEplusSD_Fakedata_SD.csv"),
                os.path.join(args.output_folder, f"{sf}_BLEplusSD_Fakedata_BLE.csv"),
                os.path.join(args.output_folder, f"{sf}_BLE_Intan.csv"),
                os.path.join(args.output_folder, f"{sf}_SD_Intan.csv")
            ]

            duration = 0
            for sample_csv in sample_csvs:
                if os.path.exists(sample_csv):
                    try:
                        df = pd.read_csv(sample_csv)
                        if 'timestamp' in df.columns:
                            current_duration = df['timestamp'].max() - df['timestamp'].min()
                            duration = max(duration, current_duration)
                    except Exception as e:
                        logging.warning(f"Error reading {sample_csv} for duration calculation: {e}")
                        continue

            if duration > 0:
                expected_packets[sf] = int(sf * duration)
                logging.info(f"Expected packets for {sf} Hz: {expected_packets[sf]}")
            else:
                expected_packets[sf] = 0
                logging.warning(f"No valid duration found for {sf} Hz. Setting expected packets to 0.")

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

        for parent_config, sf, throughput, packet_loss in results:
            if parent_config in throughput_data:
                if throughput is not None:
                    throughput_data[parent_config][sf] = {
                        'expected': expected_packets[sf],
                        'actual': throughput
                    }
                else:
                    throughput_data[parent_config][sf] = {
                        'expected': expected_packets[sf],
                        'actual': 0  # No throughput data available
                    }

                if packet_loss is not None:
                    packet_loss_data[parent_config][sf] = packet_loss
                else:
                    packet_loss_data[parent_config][sf] = expected_packets[sf]  # Assume all packets lost

        # Analyze Stability
        stability = analyze_stability(packet_loss_data)

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

        # Step 3: Generate Plots
        logging.info("Starting plot generation...")

        plot_throughput_comparison(data, args.output_folder)
        plot_packet_loss(data, args.output_folder)
        plot_combined_throughput_packet_loss(data, args.output_folder)
        plot_intan_vs_fakedata(data['throughput'], args.output_folder)
        plot_system_stability(data['stability'], args.output_folder)
        plot_throughput_efficiency(data['efficiency'], args.output_folder)
        plot_overall_performance(data['throughput'], args.output_folder)

        logging.info("Plot generation completed.")
        logging.info("All processing and plotting completed successfully.")

    except Exception as e:
        logging.error(f"An unexpected error occurred: {e}", exc_info=True)

if __name__ == "__main__":
    main()
