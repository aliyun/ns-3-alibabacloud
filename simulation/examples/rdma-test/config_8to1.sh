# INPUT FILE PATH
TOPOLOGY_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/CLOS_128_gpus_20
FLOW_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/flows/8to1.txt

# OUTPUT FILE PATH
FCT_OUTPUT_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/fct.txt
PFC_OUTPUT_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/pfc.txt

# MONITOR SETTINGS
QLEN_MON_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/qlen.txt
TX_BW_MON_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/tx_bw.txt
RX_BW_MON_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/rx_bw.txt
RATE_MON_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/rate.txt
CNP_MON_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/cnp.txt
MON_START 2000001
MON_END 5000000
QP_MON_INTERVAL 100
QLEN_MON_INTERVAL 100
BW_MON_INTERVAL 1000

# TRACE SETTINGS (see more in ns3-interface/analysis)
ENABLE_TRACE 0
TRACE_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/trace_config.txt
TRACE_OUTPUT_FILE /ns-3-alibabacloud/simulation/examples/rdma-test/outputs/mix.tr

# VAR SETTINGS
SIMULATOR_STOP_TIME 40000000000000.00
ERROR_RATE_PER_LINK 0.0000

HAS_WIN 0
GLOBAL_T 0

INT_MULTI 1
PINT_LOG_BASE 1.05

ACK_HIGH_PRIO 0

LINK_DOWN 0 0 0

KMAX_MAP 6 25000000000 400 50000000000 800 100000000000 1600 200000000000 1200 400000000000 3200 1600000000000 2400
KMIN_MAP 6 25000000000 100 50000000000 200 100000000000 400 200000000000 300 400000000000 800 1600000000000 600
PMAX_MAP 6 25000000000 0.2 50000000000 0.2 100000000000 0.2 200000000000 0.8 400000000000 0.2 1600000000000 0.2

BUFFER_SIZE 32
SWITCH_FORWARD_DELAY 1.5

# NS3 SETTINGS
ns3::QbbNetDevice::QcnEnabled true
ns3::QbbNetDevice::DynamicThreshold true

ns3::SwitchNode::PfcEnabled false

ns3::RdmaHw::Mtu 9000
ns3::RdmaHw::CcMode 13
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