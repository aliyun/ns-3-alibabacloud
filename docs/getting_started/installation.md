# Installation

> [中文版](../CN/getting_started/installation.md)

This guide covers building **ns-3-alibabacloud** on its own. The repository is a
customized ns-3 (datacenter / RDMA oriented) that also serves as the network
backend for [SimAI](https://github.com/aliyun/SimAI).

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| OS | Linux (Ubuntu 20.04+) | Tested on Ubuntu 22.04/24.04 |
| C++ compiler | g++ 8+ or clang 6+ | C++17 support required |
| CMake | 3.10+ | ns-3 CMake build system |
| Python3 | 3.6+ | Required to drive the `./ns3` wrapper |
| GTK3 / GSL / SQLite | optional | Auto-detected; enable extra features |
| GPU / CUDA | **Not required** | ns-3 is CPU-only |

## Build (standalone ns-3)

The CMake build is driven by the `./ns3` wrapper under `simulation/`:

```bash
cd simulation

# Configure (enable examples; --disable-werror is needed for ns-3.36 on
# newer compilers such as g++ 13)
./ns3 configure --enable-examples --disable-werror

# Build everything
./ns3 build
```

> Note: this tree force-includes `<cstdint>` for g++ 13 compatibility (see
> `build-support/macros-and-definitions.cmake`) and uses `--disable-werror`
> because ns-3.36 emits warnings on newer compilers. The core libraries and
> `scratch` programs build & run cleanly; for a quick smoke test use
> `run_standalone.sh` (which calls `./ns3 run scratch-simulator`).

Or use the convenience script from the repository root:

```bash
bash run_standalone.sh        # configure + build + run scratch-simulator
```

## Relationship with SimAI (astra-sim / SimCCL)

`simulation/src/applications/CMakeLists.txt` collects extra sources with a glob:

```cmake
file(GLOB ASTRA_SIM_SOURCE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/*.cc"
  ...
  "${CMAKE_CURRENT_SOURCE_DIR}/SimCCL/mock/*.cc")
```

Those `astra-sim/` and `SimCCL/` sub-trees are **not part of this repository**.
The SimAI build (`astra-sim-alibabacloud/build/astra_ns3/build.sh`) copies them
into `simulation/src/applications/` at compile time. When building ns-3 on its
own these globs simply resolve to an empty list, so the `applications` module
compiles as plain ns-3 and the repository configures/builds independently.

## Verify Installation

```bash
cd simulation
./ns3 run scratch-simulator
# Expected output: "Scratch Simulator"
```

See [Quick Start](quickstart.md) for more run examples and common commands.
