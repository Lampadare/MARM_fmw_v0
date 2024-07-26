import struct
import csv
import os
import argparse

# Constants from neural_data.h
MAX_CHANNELS = 16

def decode_binary_file(input_file, output_file):
    with open(input_file, 'rb') as bin_file, open(output_file, 'w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        
        # Write CSV header
        header = ['timestamp'] + [f'ch{i+1}' for i in range(MAX_CHANNELS)]
        csv_writer.writerow(header)
        
        # Read and decode binary data
        while True:
            # Read one NeuralData struct (36 bytes: 32 bytes for channel data + 4 bytes for timestamp)
            binary_data = bin_file.read(36)
            if not binary_data:
                break  # End of file
            
            if len(binary_data) != 36:
                print(f"Warning: Incomplete data at the end of the file. Expected 36 bytes, got {len(binary_data)}.")
                break
            
            # Unpack the binary data
            channel_data = struct.unpack('<16H', binary_data[:32])  # 16 unsigned shorts (16-bit), little-endian
            timestamp = struct.unpack('<I', binary_data[32:])[0]  # 32-bit unsigned int, little-endian
            
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