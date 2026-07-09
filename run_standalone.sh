#!/bin/bash
# ns-3-alibabacloud standalone build & run (root wrapper).
#
# Configures, builds and runs the minimal `scratch-simulator` example so the
# repository can be verified independently of the SimAI build system. The ns3
# CMake driver lives under `simulation/`, so this script changes into it once.
#
# Usage:
#   ./run_standalone.sh                 # configure + build + run scratch-simulator
#   ./run_standalone.sh <ns3-program>   # run a different scratch/example program
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SIM_DIR="$SCRIPT_DIR/simulation"
PROG="${1:-scratch-simulator}"

if [ ! -x "$SIM_DIR/ns3" ]; then
  echo "[run_standalone.sh] ns3 driver not found: $SIM_DIR/ns3" >&2
  exit 1
fi

cd "$SIM_DIR"
# --disable-werror: ns-3.36 emits warnings on newer compilers (e.g. g++ 13);
# do not treat them as errors. The tree also force-includes <cstdint> via
# build-support/macros-and-definitions.cmake for g++ 13 compatibility.
./ns3 configure --enable-examples --disable-werror
# Use `ns3 run` so only the target and its module dependencies are built.
# A full `./ns3 build` also compiles the bundled examples, some of which do not
# compile on ns-3.36 with very new compilers (e.g. g++ 13); that is an upstream
# example issue unrelated to the core libraries and scratch programs.
./ns3 run "$PROG"
