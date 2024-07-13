import struct
import csv
import os
import argparse

# Constants from neural_data.h
MAX_CHANNELS = 16
MAX_BYTES_PER_CHANNEL = 15

def decode_binary_file(input_file, output_file):
    with open(input_file, 'rb') as bin_file, open(output_file, 'w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        
        # Write CSV header
        header = ['timestamp'] + [f'ch{i+1}_byte{j+1}' for i in range(MAX_CHANNELS) for j in range(MAX_BYTES_PER_CHANNEL)]
        csv_writer.writerow(header)
        
        # Read and decode binary data
        while True:
            # Read one NeuralData struct
            binary_data = bin_file.read(4 + MAX_CHANNELS * MAX_BYTES_PER_CHANNEL)
            if not binary_data:
                break  # End of file
            
            if len(binary_data) != 4 + MAX_CHANNELS * MAX_BYTES_PER_CHANNEL:
                print(f"Warning: Incomplete data at the end of the file. Expected {4 + MAX_CHANNELS * MAX_BYTES_PER_CHANNEL} bytes, got {len(binary_data)}.")
                break
            
            # Unpack the binary data
            timestamp = struct.unpack('<I', binary_data[:4])[0]  # 32-bit unsigned int, little-endian
            channel_data = struct.unpack(f'<{MAX_CHANNELS * MAX_BYTES_PER_CHANNEL}B', binary_data[4:])
            
            # Prepare row data
            row = [timestamp] + list(channel_data)
            
            # Write to CSV
            csv_writer.writerow(row)

def main():
    parser = argparse.ArgumentParser(description='Convert binary neural data to CSV.')
    parser.add_argument('input_file', help='Path to the input binary file')
    parser.add_argument('output_file', help='Path to the output CSV file')
    args = parser.parse_args()

    if not os.path.exists(args.input_file):
        print(f"Error: Input file '{args.input_file}' does not exist.")
        return

    print(f"Converting {args.input_file} to {args.output_file}")
    decode_binary_file(args.input_file, args.output_file)
    print("Conversion complete.")

if __name__ == "__main__":
    main()