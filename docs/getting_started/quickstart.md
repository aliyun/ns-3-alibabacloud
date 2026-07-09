# Quick Start

> [中文版](../CN/getting_started/quickstart.md)

**ns-3-alibabacloud** is a customized ns-3 that extends the point-to-point
module with a datacenter / RDMA-oriented end-to-end model (QBB/PFC, ECN+CNP,
RDMA host stack, switch/NVSwitch modeling). It is the packet-level network
backend for [SimAI](https://github.com/aliyun/SimAI), but it can also be built
and run on its own.

## One-Command Standalone Run

From the repository root:

```bash
bash run_standalone.sh
# = cd simulation && ./ns3 configure --enable-examples --disable-werror \
#   && ./ns3 run scratch-simulator
```

Run a different program:

```bash
bash run_standalone.sh <program-name>
```

## Manual Flow

```bash
cd simulation

# 1. Configure once (--disable-werror: ns-3.36 on newer compilers like g++ 13)
./ns3 configure --enable-examples --disable-werror

# 2. Build
./ns3 build

# 3. Run the minimal example
./ns3 run scratch-simulator          # prints "Scratch Simulator"
```

## Common `./ns3` Commands

| Command | Purpose |
|---|---|
| `./ns3 configure [--enable-examples] [--enable-tests]` | Configure the build |
| `./ns3 build [<target>]` | Build everything or a single target |
| `./ns3 run <program>` | Build (if needed) and run a program |
| `./ns3 clean` | Remove build artifacts |
| `./ns3 show config` | Print the current configuration |
| `./ns3 show version` | Print the ns-3 version |

## Standalone vs SimAI Integration

`simulation/src/applications/CMakeLists.txt` globs `astra-sim/*.cc` and
`SimCCL/mock/*.cc`. These sub-trees are populated by the SimAI build at compile
time; when building ns-3 standalone the globs are empty and `applications`
compiles as plain ns-3. See [Installation](installation.md) for details.

## Known Limitations

- Optional modules `brite`, `click`, `mpi`, `mtp`, `openflow`, `visualizer`
  require extra dependencies and are skipped if not installed (reported at
  configure time as "Modules that cannot be built").
- A full `./ns3 build` compiles all bundled examples; a few upstream ns-3.36
  examples fail on very new compilers (e.g. g++ 13, `ns3::Event referred to as
  class`). Core libraries and `scratch` programs build fine, so
  `run_standalone.sh` uses `./ns3 run scratch-simulator`.
- Full SimAI simulation (astra-sim + SimCCL flow models over ns-3) is driven by
  the SimAI build system, not by this repository alone.
