import struct
import csv
import re
from datetime import datetime
import logging

# Constants
MAX_CHANNELS = 16
ADC_SCALE_FACTOR = 0.195  # typical scale factor RHD2000 in µV/bit
SNIPPET_DURATION = 5  # seconds

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

                        # Parse BLE logger's timestamp (we won't use it for the actual data)
                        timestamp_dt = datetime.strptime(timestamp_str, '%Y-%m-%dT%H:%M:%S.%fZ')

                        # Clean up the data (removing hyphens)
                        data_clean = data.replace('-', '')

                        # Convert hex string to bytes
                        data_bytes = bytes.fromhex(data_clean)

                        # Ensure we have enough bytes (32 bytes for 16 channels + 4 bytes for timestamp = 36 bytes total)
                        if len(data_bytes) != 36:
                            logging.warning(f"Invalid data length: {len(data_bytes)} in line: {line.strip()}")
                            continue

                        # Unpack 16 signed short integers (each is 2 bytes) from the first 32 bytes
                        channel_data = struct.unpack('<16h', data_bytes[:32])

                        # Apply scaling factor to convert to µV
                        channel_data = [value * ADC_SCALE_FACTOR for value in channel_data]

                        # Extract the actual timestamp from the last 4 bytes of the data
                        ble_timestamp = struct.unpack('<I', data_bytes[32:])[0]

                        # Create the row with the timestamp and channel data
                        row = [ble_timestamp] + channel_data
                        all_rows.append(row)

                # Write all rows to CSV at once (better for performance)
                csv_writer.writerows(all_rows)

                logging.info(f"Processed BLE file: {input_file}")

    except Exception as e:
        logging.error(f"Error processing BLE file {input_file}: {e}")
        raise


def test_ble_log_decoding():
    """
    Test function to check BLE log decoding correctness.
    """
    input_test_file = "throughputdata/b12.txt"  # Replace with your path
    output_test_file = "output.csv"  # Replace with your path

    decode_ble_log(input_test_file, output_test_file)

    # Now, check the output file manually to verify correctness

if __name__ == "__main__":
    test_ble_log_decoding()
