# INPUT FILE PATH
# The topo file is fully compatible with SimAI and can be generated using the gen_Topo_Template.py script from the SimAI master branch.
# Note that the GPU scale in the topo file must be no less than the number of GPUs participating in the collective communication task, but does not need to be exactly equal.  
# For example, it is acceptable to have 1024 GPUs defined in the topo file while only 16 GPUs are actively involved in the AllReduce operation.
TOPOLOGY_FILE mix/incast/HPN_7_0_512_gpus_8_in_one_server_with_100Gbps_A100
FLOW_FILE mix/incast/flow1.txt

# MONITOR SETTINGS
# 输出文件使用相对路径，相对于运行 ns-3 时的工作目录 simulation/
# （与上方 TOPOLOGY_FILE / FLOW_FILE 的相对基准保持一致）。
# 注意：程序会自动依据配置文件名在扩展名前插入 infix，例如 config_example.sh
#       会使 mix/fct.txt 实际落地为 mix/fct_example.txt。
FCT_OUTPUT_FILE mix/fct.txt
PFC_OUTPUT_FILE mix/pfc.txt
QLEN_MON_FILE mix/qlen.txt
BW_MON_FILE mix/bw.txt
RATE_MON_FILE mix/rate.txt
CNP_MON_FILE mix/cnp.txt
BW_MON_INTERVAL 500
QP_MON_INTERVAL 500

# Trace is disabled by default in CLEM.
ENABLE_TRACE 0
# TRACE_FILE /etc/CLEM/ns-3-alibabacloud/simulation/mix/trace1.txt
# TRACE_OUTPUT_FILE /home/tmp/astra-sim/incast/incast_sample/mix.tr

# VAR SETTINGS
SIMULATOR_STOP_TIME 40000000000000.00
ERROR_RATE_PER_LINK 0.0000

HAS_WIN 0
GLOBAL_T 0

INT_MULTI 1
PINT_LOG_BASE 1.05

ACK_HIGH_PRIO 0

LINK_DOWN 0 0 0

# switch threshold, same with SimAI
KMAX_MAP 8 94000000000 400 96000000000 300 100000000000 1600 200000000000 1200 400000000000 3200 370000000000 3200 1920000000000 3200 2000000000000 3200
KMIN_MAP 8 94000000000 100 96000000000 300 100000000000 400 200000000000 300 400000000000 800 370000000000 800 1920000000000 800 2000000000000 800
PMAX_MAP 8 94000000000 0.2 96000000000 1.0 100000000000 0.2 200000000000 0.8 400000000000 0.2 370000000000 0.2 1920000000000 0.6 2000000000000 0.6 

# buffer size of switch MMU, same with SimAI
BUFFER_SIZE 32

# new in CLEM: IbvInterface settings
ns3::IbvInterface::PollTimeInterval 100
ns3::IbvInterface::SendLatency 8000

# NS3 SETTINGS
# The following configurable parameters consistent with SimAI.
# Refer to the corresponding class definition files for details 
# ( e.g., find ns3::QbbNetDevice::QcnEnabled in ns-3-alibabacloud/simulation/src/point-to-point/model/qbb-net-device.cc)

ns3::QbbNetDevice::QcnEnabled true
ns3::QbbNetDevice::DynamicThreshold true

ns3::SwitchNode::PfcEnabled true

ns3::RdmaHw::Mtu 8192
ns3::RdmaHw::CcMode 1
ns3::RdmaHw::RateAI 5Mb/s
ns3::RdmaHw::RateHAI 50Mb/s
ns3::RdmaHw::MinRate 100Mb/s
ns3::RdmaHw::L2ChunkSize 4000
ns3::RdmaHw::L2AckInterval 1
ns3::RdmaHw::L2BackToZero false
ns3::RdmaHw::VarWin true
ns3::RdmaHw::RateBound true
ns3::RdmaHw::NicCoalesceMethod PER_QP
ns3::RdmaHw::NACKGenerationInterval 0.01
ns3::RdmaHw::GPUsPerServer 8

ns3::MellanoxDcqcn::AlphaResumInterval 1.0
ns3::MellanoxDcqcn::RateDecreaseInterval 4.0
ns3::MellanoxDcqcn::ClampTargetRate false
ns3::MellanoxDcqcn::RPTimer 900.0
ns3::MellanoxDcqcn::EwmaGain 0.00390625
ns3::MellanoxDcqcn::FastRecoveryTimes 1
ns3::Dctcp::DctcpRateAI 1000Mb/s
ns3::Hpcc::FastReact true
ns3::Hpcc::TargetUtil 0.95
ns3::Hpcc::MiThresh 0
ns3::Hpcc::MultiRate false
ns3::Hpcc::SampleFeedback false
ns3::HpccPint::PintProb 1.0
ns3::RealDcqcn::EwmaGain 0.00390625
ns3::RealDcqcn::F 1
ns3::RealDcqcn::RateUpdateDelay 300us
ns3::RealDcqcn::AlphaUpdateDelay 2.56us
ns3::RealDcqcn::BytesThreshold 524240
ns3::RealDcqcn::Clamp false