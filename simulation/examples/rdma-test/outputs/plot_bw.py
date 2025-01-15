import csv
import matplotlib.pyplot as plt
import argparse

def draw_bw_avg(input_file, output_file='output_plot.png'):
    node_bandwidths = {}

    with open(input_file, 'r') as f:
        reader = csv.reader(f)
        next(reader)  # Skip the header row
        for row in reader:
            if len(row) != 4:
                continue  # Skip improperly formatted rows
            try:
                time = float(row[0])
                node_id = int(row[1])
                _ = row[2]  # Ignore the third column
                bandwidth = float(row[3])
                if node_id not in node_bandwidths:
                    node_bandwidths[node_id] = []
                node_bandwidths[node_id].append((time, bandwidth))
            except ValueError:
                continue  # Skip rows that cannot be converted to float

    plt.figure(figsize=(10, 5))
    for node_id, data in node_bandwidths.items():
        times, bandwidths = zip(*data)  # Unpack time and bandwidth data
        if node_id != 128:  # Filter condition for node_id
            plt.plot(times, bandwidths, label=f'Node {node_id}')

    plt.legend()
    plt.title('Time vs Bandwidth')
    plt.xlabel('Time (s)')
    plt.ylabel('Bandwidth')
    plt.savefig(output_file, dpi=300, bbox_inches='tight')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Plot bandwidth data from a CSV file.")
    parser.add_argument('--input', '-i', required=True, help="Input CSV file.")
    parser.add_argument('--output', '-o', default='output_plot.png', help="Output plot file.")

    args = parser.parse_args()

    draw_bw_avg(args.input, args.output)
