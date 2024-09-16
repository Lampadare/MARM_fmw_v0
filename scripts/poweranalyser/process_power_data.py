import csv
from statistics import mean
import sys
import os
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict
from scipy.signal import savgol_filter

# Dictionary to map file names to experiment names and ordering
experiment_info = {
    '1.csv': ('BLE+SD', 1000), '2.csv': ('BLE+SD', 500), '3.csv': ('BLE+SD', 250), 
    '4.csv': ('BLE+SD', 100), '5.csv': ('BLE+SD', 20),
    '6.csv': ('SD Card Only', 20), '7.csv': ('SD Card Only', 100), '8.csv': ('SD Card Only', 250), 
    '9.csv': ('SD Card Only', 500), '10.csv': ('SD Card Only', 1000),
    '11.csv': ('BLE Only', 1000), '12.csv': ('BLE Only', 500), '13.csv': ('BLE Only', 250), 
    '14.csv': ('BLE Only', 100), '15.csv': ('BLE Only', 20),
    '16.csv': ('Idle', 0)
}

def process_power_data(file_path):
    voltage_measurements = []
    current_measurements = []
    time_measurements = []
    
    with open(file_path, 'r') as file:
        # Skip header lines
        for _ in range(6):
            next(file)
        
        reader = csv.DictReader(file)
        for row in reader:
            try:
                voltage = float(row['Volt avg 1'])
                current = float(row['Curr avg 1'])
                time = float(row['Sample']) * 0.00012288  # Convert sample to time
                voltage_measurements.append(voltage)
                current_measurements.append(current)
                time_measurements.append(time)
            except (ValueError, KeyError):
                continue
    
    if current_measurements and voltage_measurements:
        avg_voltage = mean(voltage_measurements)
        avg_current = mean(current_measurements)
        avg_power = avg_voltage * avg_current
        max_current = max(current_measurements)
        
        experiment_type, sampling_rate = experiment_info.get(os.path.basename(file_path), ("Unknown", 0))
        experiment_name = f"{experiment_type} {sampling_rate}Hz" if sampling_rate > 0 else experiment_type
        
        print(f"File: {os.path.basename(file_path)} - {experiment_name}")
        print(f"Average voltage: {avg_voltage:.6f} V")
        print(f"Average current: {avg_current:.6f} A")
        print(f"Average power: {avg_power:.6f} W")
        print(f"Max current: {max_current:.6f} A")
        print(f"Number of measurements: {len(current_measurements)}")
        print("--------------------")
        
        return {
            'name': experiment_name,
            'type': experiment_type,
            'rate': sampling_rate,
            'avg_power': avg_power,
            'time': time_measurements[:int(2/0.00012288)],  # 2 seconds of data
            'current': current_measurements[:int(2/0.00012288)]  # 2 seconds of data
        }
    else:
        print(f"No valid measurements found in {file_path}")
        return None

def create_time_series_plots(data):
    experiment_types = sorted(set(exp['type'] for exp in data if exp['type'] != 'Idle'))
    
    for exp_type in experiment_types:
        type_data = [exp for exp in data if exp['type'] == exp_type]
        type_data.sort(key=lambda x: x['rate'], reverse=True)
        
        num_rates = len(type_data)
        fig, axs = plt.subplots(num_rates, 1, figsize=(14, 2.5*num_rates + 0.5), sharex=True)
        
        if num_rates == 1:
            axs = [axs]
        
        # Create lines for the legend
        raw_line = None
        avg_line = None
        
        for i, exp in enumerate(type_data):
            ax = axs[i]
            time = exp['time']
            current = exp['current']
            
            # Plot raw data
            raw, = ax.plot(time, current, alpha=0.5)
            if raw_line is None:
                raw_line = raw
            
            # Calculate and plot moving average
            window_size = 101  
            moving_avg = savgol_filter(current, window_size, 3)
            avg, = ax.plot(time, moving_avg, linewidth=2)
            if avg_line is None:
                avg_line = avg
            
            ax.set_title(f'{exp_type} - {exp["rate"]}Hz', fontsize=19)
            
            # Reduce the number of y-ticks
            ax.yaxis.set_major_locator(plt.MaxNLocator(5))
            
            # Set font size for tick labels
            ax.tick_params(axis='both', which='major', labelsize=16)
            
            # Add faint vertical grid lines
            ax.grid(axis='x', linestyle='--', alpha=0.3)
        
        # Add a single centered y-label for all subplots
        fig.text(0.02, 0.5, 'Current (A)', va='center', rotation='vertical', fontsize=20)
        
        # Add a single x-label
        axs[-1].set_xlabel('Time (s)', fontsize=20)
        
        # Add a single legend for the entire plot at the bottom
        fig.legend([raw_line, avg_line], ['Raw', 'Moving Average'], 
                   loc='lower center', bbox_to_anchor=(0.5, 0), ncol=2, fontsize=16)
        
        plt.tight_layout()
        
        # Adjust the layout to make room for the legend and y-label
        plt.subplots_adjust(left=0.08, right=0.98, top=0.95, bottom=0.1, hspace=0.3)
        
        plt.savefig(f'time_series_{exp_type.replace(" ", "_")}.png', dpi=300, bbox_inches='tight')
        plt.close()

def create_power_bar_plot(data):
    VOLTAGE = 3.7  # Constant voltage in Volts

    # Define colors for each experiment type
    color_map = {
        'BLE+SD': '#1f77b4',     # Blue
        'BLE Only': '#2ca02c',   # Green
        'SD Card Only': '#ff7f0e', # Orange
        'Idle': '#d62728'        # Red
    }

    # Define alpha values for each sampling rate
    alpha_map = {1000: 1.0, 500: 0.9, 250: 0.8, 100: 0.7, 20: 0.6, 0: 1.0}  # 0 is for Idle

    sorted_data = sorted(data, key=lambda x: (x['type'], -x['rate']))
    sorted_data = [exp for exp in sorted_data if exp['type'] != 'Idle'] + [exp for exp in sorted_data if exp['type'] == 'Idle']
    
    plt.figure(figsize=(16, 10))  # Increased figure size
    
    x_positions = range(len(sorted_data))
    for i, exp in enumerate(sorted_data):
        color = color_map[exp['type']]
        alpha = alpha_map[exp['rate']]
        plt.bar(i, exp['avg_power'], color=color, alpha=alpha)
    
    plt.xlabel('Sampling Frequency', fontsize=18)
    plt.ylabel('Average Power (W)', fontsize=18)
    plt.title('Average Power Consumption by Experiment', fontsize=26)
    
    # Set x-ticks to show frequencies with "Hz"
    frequencies = [f"{exp['rate']}Hz" if exp['rate'] != 0 else "Idle" for exp in sorted_data]
    plt.xticks(x_positions, frequencies, rotation=45, ha='right', fontsize=18)
    plt.yticks(fontsize=18)
    
    # Find the maximum power value
    max_power = max(exp['avg_power'] for exp in sorted_data)
    
    # Set y-axis limit to 130% of the maximum power value
    plt.ylim(0, max_power * 1.3)
    
    for i, exp in enumerate(sorted_data):
        height = exp['avg_power']
        current_ma = (height / VOLTAGE) * 1000
        plt.text(i, height, f'{height:.6f} W\n{current_ma:.1f} mA',
                 ha='center', va='bottom', rotation=90, fontsize=16)
    
    # Add legend for colors only
    legend_elements = [plt.Rectangle((0,0),1,1, facecolor=color, edgecolor='none', label=exp_type)
                       for exp_type, color in color_map.items()]
    
    plt.legend(handles=legend_elements, loc='upper right', fontsize=16)
    
    plt.tight_layout()
    plt.savefig('power_bar_plot.png', dpi=600, bbox_inches='tight')
    plt.close()

def create_power_breakdown_plot(data):
    VOLTAGE = 3.7  # Constant voltage in Volts

    # Group experiments by type and frequency
    grouped_data = defaultdict(lambda: defaultdict(list))
    frequencies = set()
    for exp in data:
        grouped_data[exp['type']][exp['rate']].append(exp['avg_power'])
        if exp['type'] != 'Idle':
            frequencies.add(exp['rate'])
    
    # Calculate average power for each group and frequency
    avg_powers = {t: {f: mean(v) for f, v in freq_data.items()} for t, freq_data in grouped_data.items()}
    
    # Sort frequencies
    frequencies = sorted(frequencies)
    
    # Define consistent colors for each component
    colors = {
        'Idle': '#1f77b4',    # Blue
        'SD Card': '#ff7f0e', # Orange
        'BLE': '#2ca02c',     # Green
    }
    
    # Create a taller stacked bar chart
    fig, ax = plt.subplots(figsize=(14, 10))  # Increased size for better visibility
    bar_width = 0.6
    index = np.arange(len(frequencies))
    
    for i, freq in enumerate(frequencies):
        idle_power = avg_powers['Idle'][0]  # Assume idle power is constant across frequencies
        ble_sd_power = avg_powers['BLE+SD'][freq]
        ble_only_power = avg_powers['BLE Only'][freq]
        sd_only_power = avg_powers['SD Card Only'][freq]
        
        # Calculate the power contribution of each component
        sd_power = max(0, sd_only_power - idle_power)
        ble_power = max(0, ble_only_power - idle_power)
        
        # Create stacked bars
        bottom = 0
        for component, color in colors.items():
            if component == 'Idle':
                height = idle_power
            elif component == 'SD Card':
                height = sd_power
            elif component == 'BLE':
                height = ble_power
            
            ax.bar(index[i], height, bar_width, bottom=bottom, color=color, label=component if i == 0 else "")
            bottom += height
        
        # Add text labels
        total_height = idle_power + sd_power + ble_power
        total_current_ma = (total_height / VOLTAGE) * 1000
        ax.text(index[i], total_height, f'{total_height:.4f}W\n{total_current_ma:.1f}mA', ha='center', va='bottom', fontsize=22)
    
    ax.set_ylabel('Average Power (W)', fontsize=22)
    ax.set_title('Average Power Consumption Breakdown by Frequency', fontsize=26)
    ax.set_xticks(index)
    ax.set_xticklabels([f'{freq}Hz' for freq in frequencies], fontsize=22)
    ax.tick_params(axis='y', labelsize=22)
    ax.legend(fontsize=22)
    
    # Adjust y-axis to ensure nothing is cropped
    ylim = ax.get_ylim()
    ax.set_ylim(ylim[0], ylim[1] * 1.1)  # Add 10% padding to the top
    
    plt.tight_layout()
    plt.savefig('power_breakdown_plot.png', dpi=300, bbox_inches='tight')
    plt.close()
    
    # Print detailed breakdown
    print("\nDetailed Average Power Consumption Breakdown:")
    for freq in frequencies:
        idle_power = avg_powers['Idle'][0]
        ble_sd_power = avg_powers['BLE+SD'][freq]
        ble_only_power = avg_powers['BLE Only'][freq]
        sd_only_power = avg_powers['SD Card Only'][freq]
        
        sd_power = max(0, sd_only_power - idle_power)
        ble_power = max(0, ble_only_power - idle_power)
        
        total_power = idle_power + sd_power + ble_power
        total_current_ma = (total_power / VOLTAGE) * 1000
        
        print(f"\n{freq}Hz:")
        print(f"  Idle (Avg): {idle_power:.4f}W ({idle_power/VOLTAGE*1000:.1f}mA)")
        print(f"  SD Card (Avg): {sd_power:.4f}W ({sd_power/VOLTAGE*1000:.1f}mA)")
        print(f"  BLE (Avg): {ble_power:.4f}W ({ble_power/VOLTAGE*1000:.1f}mA)")
        print(f"  Total (Avg): {total_power:.4f}W ({total_current_ma:.1f}mA)")

def create_current_box_plot(data):
    sorted_data = sorted(data, key=lambda x: (x['type'], -x['rate']))
    sorted_data = [exp for exp in sorted_data if exp['type'] != 'Idle'] + [exp for exp in sorted_data if exp['type'] == 'Idle']
    
    plt.figure(figsize=(12, 6))
    current_data = [exp['current'] for exp in sorted_data]
    plt.boxplot(current_data, labels=[exp['name'] for exp in sorted_data])
    plt.xlabel('Experiment')
    plt.ylabel('Current (A)')
    plt.title('Distribution of Current Measurements by Experiment')
    plt.xticks(rotation=45, ha='right')
    plt.tight_layout()
    plt.savefig('current_box_plot.png')
    plt.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python script_name.py <csv_file1> <csv_file2> ...")
        sys.exit(1)
    
    all_data = []
    for file_path in sys.argv[1:]:
        if file_path.endswith('.csv'):
            result = process_power_data(file_path)
            if result:
                all_data.append(result)
        else:
            print(f"Skipping {file_path}: not a CSV file")
    
    if all_data:
        create_time_series_plots(all_data)
        create_power_bar_plot(all_data)
        create_power_breakdown_plot(all_data)
        create_current_box_plot(all_data)
        print("Plots have been saved as PNG files in the current directory.")
    else:
        print("No valid data to plot.")

if __name__ == "__main__":
    main()