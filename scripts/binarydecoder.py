import struct
import csv
import os
import argparse
import glob

# Constants from neural_data.h
MAX_CHANNELS = 16

def decode_binary_files(input_folder, output_file):
    with open(output_file, 'w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        
        # Write CSV header
        header = ['timestamp'] + [f'ch{i+1}' for i in range(MAX_CHANNELS)]
        csv_writer.writerow(header)
        
        # Get all data_xx.bin files in the input folder, sorted numerically
        bin_files = sorted(glob.glob(os.path.join(input_folder, 'data_*.bin')),
                           key=lambda x: int(x.split('_')[-1].split('.')[0]))
        
        for bin_file_path in bin_files:
            print(f"Processing {bin_file_path}")
            with open(bin_file_path, 'rb') as bin_file:
                # Read and decode binary data
                while True:
                    # Read one NeuralData struct (36 bytes: 32 bytes for channel data + 4 bytes for timestamp)
                    binary_data = bin_file.read(36)
                    if not binary_data:
                        break  # End of file
                    
                    if len(binary_data) != 36:
                        print(f"Warning: Incomplete data at the end of {bin_file_path}. Expected 36 bytes, got {len(binary_data)}.")
                        break
                    
                    # Unpack the binary data
                    channel_data = struct.unpack('<16H', binary_data[:32])  # 16 unsigned shorts (16-bit), little-endian
                    timestamp = struct.unpack('<I', binary_data[32:])[0]  # 32-bit unsigned int, little-endian
                    
                    # Prepare row data
                    row = [timestamp] + list(channel_data)
                    
                    # Write to CSV
                    csv_writer.writerow(row)

def main():
    parser = argparse.ArgumentParser(description='Convert multiple binary neural data files to a single CSV.')
    parser.add_argument('input_folder', help='Path to the folder containing input binary files')
    parser.add_argument('output_file', help='Path to the output CSV file')
    args = parser.parse_args()

    if not os.path.exists(args.input_folder):
        print(f"Error: Input folder '{args.input_folder}' does not exist.")
        return

    print(f"Converting binary files from {args.input_folder} to {args.output_file}")
    decode_binary_files(args.input_folder, args.output_file)
    print("Conversion complete.")

if __name__ == "__main__":
    main()