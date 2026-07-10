<p align="left"><a href="README.md">English</a> ｜ 中文</p>

# NS-3-ALIBABACLOUD

本仓库是一个基于 NS3 的网络模拟器，作为 [SimAI](https://github.com/aliyun/SimAI) 的网络后端。

我们发布了一个新的开发分支 [**dev/qp**](https://github.com/aliyun/ns-3-alibabacloud/tree/dev/qp)，包含以下增强（由维护者 [**@MXtremist**](https://github.com/MXtremist) 提供）：
1. **QP 逻辑支持**：支持基于真实 RDMA 逻辑创建和销毁 QP，允许一对 QP 承载多条消息。
2. **网卡 CC 配置**：支持 perIP 或 perQP 设置，提供更高的灵活性。
3. **优化的调度逻辑**：遵循 Max-Min（最大最小）原则，解决网络资源分配中利用率不足与不公平的问题。
4. **CC 模块解耦**：提升模块化程度与效率。

## 与上游 ns-3 的关键差异（聚焦 `simulation/src/point-to-point/model/`）

相比原始 [ns-3](https://www.nsnam.org/)，本仓库对 point-to-point 模块进行了扩展，加入了面向 **数据中心 / RDMA** 的端到端模型。主要新增位于 `simulation/src/point-to-point/model/`，包括：

- **QBB/PFC + 多优先级队列**：8 个优先级队列、PAUSE/RESUME（类 PFC）处理，以及每个端口/网卡上的优先级感知调度。
- **ECN + CNP（类 QCN）反馈**：交换机侧基于队列占用的 ECN 标记，接收端侧的 ECN 统计；拥塞反馈通过 CNP 报文传递。
- **RDMA 主机协议栈（QP 级）**：QP/RxQP 建模、窗口/在途（on-the-fly）控制、ACK/NACK 处理，以及多种网卡拥塞控制（CC）模式（如 DCQCN/HPCC/TIMELY/DCTCP/HPCC-PINT）。
- **交换机与 NVSwitch 建模**：ECMP 转发、缓冲/MMU 准入控制、PFC 触发/恢复逻辑，以及（可选的）用于 HPCC(-PINT) 的 INT/PINT 元数据注入。

## 模块地图（各文件/类的作用）

- **`qbb-net-device.{h,cc}`（`QbbNetDevice`、`RdmaEgressQueue`）**
  - **作用**：在 `PointToPointNetDevice` 之上实现的、支持 8 优先级的 QBB 网络设备。它拦截接收以处理 PFC，并从以下两者之一调度发送：
    - **主机/网卡**：`RdmaEgressQueue`（高优先级 ACK/NACK 队列 + 跨 QP 轮询），或
    - **交换机端口**：`BEgressQueue`（跨优先级队列轮询）。
    当启用 NVLS 时，它还支持 NVSwitch “交换机充当主机” 的发送路径。
  - **关键属性**：`QbbEnabled`、`QcnEnabled`、`DynamicThreshold`、`PauseTime`、`NVLS_enable`。
  - **关键集成回调**：`m_rdmaReceiveCb`（将非 PFC 报文交付给 `RdmaHw`）、`m_rdmaSentCb`（逐报文发送完成）、`m_rdmaPktSent`（更新 QP 节奏/下一次可用时间）、`m_rdmaLinkDownCb`。
  - **扩展位置**：
    - **调度 / 优先级规则**：`DequeueAndTransmit()` 与 `RdmaEgressQueue::GetNextQindex()`
    - **PFC 行为**：`Receive()` 与 `SendPfc()`

- **`qbb-channel.{h,cc}` / `qbb-remote-channel.{h,cc}`**
  - **作用**：`QbbNetDevice` 的点到点信道；`QbbRemoteChannel` 使用 MPI（`MpiInterface::SendPacket`）进行分布式仿真。
  - **扩展位置**：`TransmitStart()` 中的链路行为/交付路径。

- **`switch-node.{h,cc}`（`SwitchNode`）**
  - **作用**：交换机流水线（`nodeType = 1`）：ECMP 转发（五元组哈希）、经 MMU 的准入控制、PFC 暂停/恢复生成、可选 ECN 标记，以及出队时的 INT/PINT 注入（用于 HPCC / HPCC-PINT）。
  - **关键属性**：`EcnEnabled`、`CcMode`、`AckHighPrio`、`MaxRtt`。
  - **扩展位置**：
    - **转发 / ECMP**：`GetOutDev()`、`EcmpHash()`、`AddTableEntry()`
    - **ECN / PFC / INT-PINT 注入**：`SwitchNotifyDequeue()`

- **`switch-mmu.{h,cc}`（`SwitchMmu`）**
  - **作用**：交换机缓冲/MMU 模型：入向/出向计量、共享缓冲与 headroom、暂停/恢复决策、ECN 标记概率曲线（`kmin/kmax/pmax`），以及 PFC 阈值计算。
  - **关键配置 API**：`ConfigBufferSize()`、`ConfigHdrm()`、`ConfigNPort()`、`ConfigEcn()`。
  - **扩展位置**：在此实现新的缓冲管理 / PFC 阈值公式 / ECN 曲线。

- **`nvswitch-node.{h,cc}`（`NVSwitchNode`）**
  - **作用**：NVSwitch 节点模型（`nodeType = 2`），用于服务器内 GPU 通信（与 `RdmaHw` / `QbbNetDevice` 中的 NVLS 路由逻辑配合）。
  - **扩展位置**：NVSwitch 转发/准入/监控（入口点与 `SwitchNode` 类似，但当前不做 ECN/INT 注入）。

- **`rdma-hw.{h,cc}`（`RdmaHw`）**
  - **作用**：主机 RDMA 核心：QP 创建/删除、报文构造（PPP + IPv4 + UDP + SeqTs）、ACK/NACK 处理、CNP 处理、逐 QP 的 CC 算法，以及到网卡的路由（含 NVSwitch 路由表）。
  - **关键属性**：`CcMode`、`Mtu`、`MinRate`、`L2ChunkSize`、`L2AckInterval`、`L2BackToZero`，以及 DCQCN/TIMELY/DCTCP/HPCC/PINT 的 CC 专用参数（见 `GetTypeId()`）。
  - **使用的协议号（IPv4 Protocol 字段）**：
    - **UDP 数据**：`0x11`
    - **CNP**：`0xFF`
    - **PFC**：`0xFE`
    - **ACK**：`0xFC`
    - **NACK**：`0xFD`
  - **扩展位置**：
    - **新增 CC 算法**：添加 `HandleAckX/UpdateRateX`（以及可选的 CNP 钩子），并在 `ReceiveAck()`/`ReceiveCnp()` 中按 `m_cc_mode` 分派。
    - **新增/修改路由（含 NVSwitch/NVLS）**：`GetNicIdxOfQp()`、`GetNicIdxOfRxQp()`、`AddTableEntry()`、`RedistributeQp()`。

- **`rdma-driver.{h,cc}`（`RdmaDriver`）**
  - **作用**：`Node`/网卡 与 `RdmaHw` 之间的连接层：构建网卡/QP 组，并暴露 QP 生命周期追踪（`QpComplete`、`SendComplete`）。
  - **扩展位置**：在此围绕 QP 生命周期添加更上层的可观测性或面向应用的回调。

- **`rdma-queue-pair.{h,cc}`（`RdmaQueuePair`、`RdmaRxQueuePair`、`RdmaQueuePairGroup`）**
  - **作用**：逐 QP 与逐 RxQP 的状态（窗口、速率、已确认序号，以及各 CC 算法状态：DCQCN alpha/targetRate、HPCC 逐跳状态、TIMELY RTT 跟踪、DCTCP alpha/ecnCnt、HPCC-PINT 状态）。
  - **扩展位置**：若新 CC 需要额外的逐 QP 状态，在此添加。

- **头文件 / 工具**
  - **`qbb-header.{h,cc}`**：ACK/NACK 头（PG/seq/CNP 标志 + 可选 INT 头）。
  - **`cn-header.{h,cc}`**：CNP 头（反馈字段：`fid/qIndex/ecnbits/qfb/total`）。
  - **`pause-header.{h,cc}`**：PFC 暂停头（`time/qlen/qindex`）。
  - **`pint.{h,cc}`**：PINT 编解码工具。
  - **`trace-format.h`**：离线分析器使用的二进制追踪记录结构 `TraceFormat`。

## 在何处实现新功能（快速指南）

- **新增主机侧拥塞控制（CC）**
  - **主要**：`rdma-hw.{h,cc}`（算法 + 按 `CcMode` 分派）
  - **通常需要**：`rdma-queue-pair.h`（新的逐 QP 状态）
  - **若需要交换机反馈**：`switch-node.cc`（INT/PINT 或新的标记）

- **修改交换机行为（缓冲/ECN/PFC）**
  - **主要**：`switch-mmu.{h,cc}`（阈值/曲线/公式）
  - **标记/注入发生处**：`switch-node.cc::SwitchNotifyDequeue()`
  - **准入/优先级应用处**：`switch-node.cc::SendToDev()`

- **引入新的控制报文/头**
  - **主要**：在 `model/` 中新增一个 `*Header`（参照 `CnHeader` / `PauseHeader`）
  - **解析/分派**：通常在 `QbbNetDevice::Receive()`（设备级）或 `RdmaHw::Receive()`（主机协议栈）
  - **注意**：若需由 `CustomHeader` 解析，还需扩展 `custom-header` 实现（在本目录之外）。

# 联系我们

如有任何问题，请发送邮件至 Gang Lu (yunding.lg@alibaba-inc.com)、Feiyang Xue (xuefeiyang.xfy@alibaba-inc.com) 或 Qingxu Li (qingxu.lqx@alibaba-inc.com)。

欢迎加入 SimAI 社区交流群，左侧为钉钉群，右侧为微信群。

<div style="display: flex; justify-content: flex-start; align-items: center; gap: 20px; margin-left: 20px;">
    <img src="./docs/images/simai_dingtalk.jpg" alt="SimAI DingTalk" style="width: 300px; height: auto;">
    <img src="./docs/images/simai_wechat.jpg" alt="SimAI WeChat" style="width: 300px; height: auto;">
</div>

<br/>
