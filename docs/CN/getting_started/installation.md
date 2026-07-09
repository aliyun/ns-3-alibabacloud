# 安装指南

> [English Version](../../getting_started/installation.md)

本指南介绍如何**独立构建** ns-3-alibabacloud。该仓库是定制版 ns-3（面向数据中心 /
RDMA），同时作为 [SimAI](https://github.com/aliyun/SimAI) 的网络后端。

## 前置条件

| 要求 | 版本 | 说明 |
|---|---|---|
| 操作系统 | Linux（Ubuntu 20.04+） | 已在 Ubuntu 22.04/24.04 测试 |
| C++ 编译器 | g++ 8+ 或 clang 6+ | 需支持 C++17 |
| CMake | 3.10+ | ns-3 的 CMake 构建系统 |
| Python3 | 3.6+ | 驱动 `./ns3` 封装脚本所需 |
| GTK3 / GSL / SQLite | 可选 | 自动探测；启用额外特性 |
| GPU / CUDA | **不需要** | ns-3 为纯 CPU |

## 构建（独立 ns-3）

CMake 构建由 `simulation/` 下的 `./ns3` 封装脚本驱动：

```bash
cd simulation

# 配置（启用 examples；--disable-werror 用于新编译器如 g++ 13 上的 ns-3.36）
./ns3 configure --enable-examples --disable-werror

# 全量构建
./ns3 build
```

> 注：本仓库为 g++ 13 兼容性强制包含 `<cstdint>`（见
> `build-support/macros-and-definitions.cmake`），并使用 `--disable-werror`，
> 因为 ns-3.36 在新编译器上会产生告警。核心库与 `scratch` 程序可正常构建与运行；
> 快速冒烟请用 `run_standalone.sh`（内部调用 `./ns3 run scratch-simulator`）。

或从仓库根目录使用便捷脚本：

```bash
bash run_standalone.sh        # 配置 + 构建 + 运行 scratch-simulator
```

## 与 SimAI（astra-sim / SimCCL）的关系

`simulation/src/applications/CMakeLists.txt` 通过 glob 收集额外源文件：

```cmake
file(GLOB ASTRA_SIM_SOURCE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/astra-sim/system/*.cc"
  ...
  "${CMAKE_CURRENT_SOURCE_DIR}/SimCCL/mock/*.cc")
```

其中 `astra-sim/` 与 `SimCCL/` 子目录**并不属于本仓库**。SimAI 构建流程
（`astra-sim-alibabacloud/build/astra_ns3/build.sh`）会在编译时把它们复制到
`simulation/src/applications/` 下。独立构建 ns-3 时这些 glob 结果为空，因此
`applications` 模块作为纯 ns-3 编译，仓库可独立完成配置/构建。

## 验证安装

```bash
cd simulation
./ns3 run scratch-simulator
# 预期输出："Scratch Simulator"
```

更多运行示例与常用命令见 [快速开始](quickstart.md)。
