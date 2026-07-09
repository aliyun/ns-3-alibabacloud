# 快速开始

> [English Version](../../getting_started/quickstart.md)

**ns-3-alibabacloud** 是定制版 ns-3，在 point-to-point 模块之上扩展了面向数据中心 /
RDMA 的端到端模型（QBB/PFC、ECN+CNP、RDMA 主机栈、交换机/NVSwitch 建模）。它是
[SimAI](https://github.com/aliyun/SimAI) 的分组级网络后端，也可以独立构建与运行。

## 一键独立运行

在仓库根目录：

```bash
bash run_standalone.sh
# = cd simulation && ./ns3 configure --enable-examples --disable-werror \
#   && ./ns3 run scratch-simulator
```

运行其它程序：

```bash
bash run_standalone.sh <程序名>
```

## 手动流程

```bash
cd simulation

# 1. 配置（一次即可；--disable-werror 用于 g++ 13 等新编译器上的 ns-3.36）
./ns3 configure --enable-examples --disable-werror

# 2. 构建
./ns3 build

# 3. 运行最小示例
./ns3 run scratch-simulator          # 输出 "Scratch Simulator"
```

## 常用 `./ns3` 命令

| 命令 | 用途 |
|---|---|
| `./ns3 configure [--enable-examples] [--enable-tests]` | 配置构建 |
| `./ns3 build [<目标>]` | 全量或单目标构建 |
| `./ns3 run <程序>` | 构建（如需）并运行程序 |
| `./ns3 clean` | 清除构建产物 |
| `./ns3 show config` | 打印当前配置 |
| `./ns3 show version` | 打印 ns-3 版本 |

## 独立运行 vs SimAI 集成

`simulation/src/applications/CMakeLists.txt` 会 glob `astra-sim/*.cc` 与
`SimCCL/mock/*.cc`。这些子目录由 SimAI 构建在编译时填充；独立构建 ns-3 时这些
glob 结果为空，`applications` 作为纯 ns-3 编译。详见 [安装指南](installation.md)。

## 已知限制

- 可选模块 `brite`、`click`、`mpi`、`mtp`、`openflow`、`visualizer` 需额外依赖，
  未安装时会被跳过（配置阶段报告为 "Modules that cannot be built"）。
- 完整的 `./ns3 build` 会编译全部自带 examples；少数 ns-3.36 上游 example 在很新
  编译器（如 g++ 13，`ns3::Event referred to as class`）上会失败。核心库与
  `scratch` 程序可正常构建，因此 `run_standalone.sh` 使用 `./ns3 run scratch-simulator`。
- 完整 SimAI 仿真（astra-sim + SimCCL flow model 跑在 ns-3 上）由 SimAI 构建系统
  驱动，而非本仓库单独完成。
