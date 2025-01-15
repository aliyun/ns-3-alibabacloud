# Quick Start Guide

This guide will help you quickly get started with running the ns-3-alibabacloud simulation (incast).

## Prerequisites

- Git
- Python3
- NS-3 development environment

## Steps to Run

1. Clone the repository:
```bash
git clone https://github.com/aliyun/ns-3-alibabacloud
git checkout dev/qp
```

2. Configure the paths:

Open [source_dir]/ns-3-alibabacloud/simulation/examples/rdma-test/config_8to1.sh
Update all path prefixes to ensure correct paths in your environment.

3. Run incast example:
```bash
cd ./ns-3-alibabacloud/simulation
./ns3 configure -d default --enable-mtp
./ns3 run 'scratch/NormalNetwork examples/rdma-test/config_8to1.sh'
```

4. Get results:
```bash
cd examples/rdma-test/outputs/
python3 plot_bw.py -i bw_8to1.txt
```