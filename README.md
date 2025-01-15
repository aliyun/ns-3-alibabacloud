# NS-3-ALIBABACLOUD

This repository contains an NS3-based network simulator that acts as a network backend for [SimAI](https://github.com/aliyun/SimAI).

We have released a new dev branch [**dev/qp**](https://github.com/aliyun/ns-3-alibabacloud/tree/dev/qp) featuring the following enhancements (From maintainer [**@MXtremist**](https://github.com/MXtremist)):
1. **QP Logic Support**: Enables creation and destruction of QPs based on actual RDMA logic, allowing multiple messages to be carried by a pair of QPs.
2. **NIC CC Configuration**: Supports perIP or perQP settings for enhanced flexibility.
3. **Optimized Scheduling Logic**: Adheres to the Max-Min principle, resolving issues of underutilization and unfairness in network resource allocation.
4. **Decoupling of the CC Module**: For improved modularity and efficiency.

Welcome to join the SimAI community chat groups, with the DingTalk group on the left and the WeChat group on the right.

<div style="display: flex; justify-content: flex-start; align-items: center; gap: 20px; margin-left: 20px;">
    <img src="./docs/images/simai_dingtalk.jpg" alt="SimAI DingTalk" style="width: 300px; height: auto;">
    <img src="./docs/images/simai_wechat.jpg" alt="SimAI WeChat" style="width: 300px; height: auto;">
</div>

<br/>