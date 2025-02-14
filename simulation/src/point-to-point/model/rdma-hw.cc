#include "rdma-hw.h"
#include <ns3/ipv4-header.h>
#include <ns3/simple-seq-ts-header.h>
#include <ns3/simulator.h>
#include <ns3/udp-header.h>
#include <iostream> // debug
#include "cn-header.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/ppp-header.h"
#include "ns3/uinteger.h"
#include "ppp-header.h"
#include "qbb-header.h"
#include "rdma-congestion-ops.h"
#include "rdma-queue-pair.h"
#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#endif

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("RdmaHw");
NS_OBJECT_ENSURE_REGISTERED(RdmaHw);

TypeId RdmaHw::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::RdmaHw")
          .SetParent<Object>()
          .AddAttribute(
              "MinRate",
              "Minimum rate of a throttled flow",
              DataRateValue(DataRate("100Mb/s")),
              MakeDataRateAccessor(&RdmaHw::m_minRate),
              MakeDataRateChecker())
          .AddAttribute(
              "Mtu",
              "Mtu.",
              UintegerValue(1000),
              MakeUintegerAccessor(&RdmaHw::m_mtu),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "CcMode",
              "which mode of DCQCN is running",
              UintegerValue(0),
              MakeUintegerAccessor(&RdmaHw::m_cc_mode),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "NACKGenerationInterval",
              "The NACK Generation interval",
              DoubleValue(500.0),
              MakeDoubleAccessor(&RdmaHw::m_nack_interval),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "L2ChunkSize",
              "Layer 2 chunk size. Disable chunk mode if equals to 0.",
              UintegerValue(0),
              MakeUintegerAccessor(&RdmaHw::m_chunk),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "L2AckInterval",
              "Layer 2 Ack intervals. Disable ack if equals to 0.",
              UintegerValue(0),
              MakeUintegerAccessor(&RdmaHw::m_ack_interval),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "L2BackToZero",
              "Layer 2 go back to zero transmission.",
              BooleanValue(false),
              MakeBooleanAccessor(&RdmaHw::m_backto0),
              MakeBooleanChecker())
          .AddAttribute(
              "RateAI",
              "Rate increment unit in AI period",
              DataRateValue(DataRate("5Mb/s")),
              MakeDataRateAccessor(&RdmaHw::m_rai),
              MakeDataRateChecker())
          .AddAttribute(
              "RateHAI",
              "Rate increment unit in hyperactive AI period",
              DataRateValue(DataRate("50Mb/s")),
              MakeDataRateAccessor(&RdmaHw::m_rhai),
              MakeDataRateChecker())
          .AddAttribute(
              "VarWin",
              "Use variable window size or not",
              BooleanValue(false),
              MakeBooleanAccessor(&RdmaHw::m_var_win),
              MakeBooleanChecker())
          .AddAttribute(
              "RateBound",
              "Bound packet sending by rate, for test only",
              BooleanValue(true),
              MakeBooleanAccessor(&RdmaHw::m_rateBound),
              MakeBooleanChecker())
          .AddAttribute(
              "GPUsPerServer",
              "the number of gpus in a server, used for routing",
              UintegerValue(1),
              MakeUintegerAccessor(&RdmaHw::m_gpus_per_server),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TotalPauseTimes",
              "The number of pause times to simulate PFC pause due to PCIe",
              UintegerValue(0),
              MakeUintegerAccessor(&RdmaHw::m_total_pause_times),
              MakeUintegerChecker<uint64_t>())
          .AddAttribute(
              "NVLS_enable",
              "NVLS enable info",
              UintegerValue(0),
              MakeUintegerAccessor(&RdmaHw::nvls_enable),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "NicCoalesceMethod",
              "The Nic's coalesce method",
              EnumValue(NicCoalesceMethod::PER_QP),
              MakeEnumAccessor(&RdmaHw::m_nic_coalesce_method),
              MakeEnumChecker(
                  NicCoalesceMethod::PER_QP,
                  "PER_QP",
                  NicCoalesceMethod::PER_IP,
                  "PER_IP"))
          .AddAttribute(
              "CnpInterval",
              "The Cnp interval",
              TimeValue(MicroSeconds(4)),
              MakeTimeAccessor(&RdmaHw::m_cnp_interval),
              MakeTimeChecker());
  return tid;
}

RdmaHw::RdmaHw() {}

void RdmaHw::enable_nvls() {
  nvls_enable = 1;
}

void RdmaHw::disable_nvls() {
  nvls_enable = 0;
}

void RdmaHw::add_nvswitch(uint32_t nvswitch_id) {
  nvswitch_set.insert(nvswitch_id);
}

void RdmaHw::SetNode(Ptr<Node> node) {
  m_node = node;
}
void RdmaHw::Setup(
    QpCompleteCallback cb,
    SendCompleteCallback send_cb,
    MessageCompleteCallback message_cb) {
  tx_bytes.resize(m_nic.size());
  last_tx_bytes.resize(m_nic.size());
  rx_bytes.resize(m_nic.size());
  last_rx_bytes.resize(m_nic.size());
  m_nic_peripTable.resize(m_nic.size());
  for (uint32_t i = 0; i < m_nic.size(); i++) {
    tx_bytes[i] = 0;
    last_tx_bytes[i] = 0;
    rx_bytes[i] = 0;
    last_rx_bytes[i] = 0;
    Ptr<QbbNetDevice> dev = m_nic[i].dev;
    if (dev == NULL)
      continue;
    // share data with NIC
    dev->m_rdmaEQ->m_qpGrp = m_nic[i].qpGrp;
    // setup callback
    dev->m_rdmaReceiveCb = MakeCallback(&RdmaHw::Receive, this);
    dev->m_rdmaSentCb = MakeCallback(&RdmaHw::SendPacketComplete, this);
    dev->m_rdmaLinkDownCb = MakeCallback(&RdmaHw::SetLinkDown, this);
    dev->m_rdmaPktSent = MakeCallback(&RdmaHw::PktSent, this);
    dev->m_rdmaUpdateTxBytes = MakeCallback(&RdmaHw::UpdateTxBytes, this);
    dev->m_rdmaUpdateRxBytes = MakeCallback(&RdmaHw::UpdateRxBytes, this);
    // config NIC
    dev->m_rdmaEQ->m_rdmaGetNxtPkt = MakeCallback(&RdmaHw::GetNxtPacket, this);
  }
  // setup qp complete callback
  m_qpCompleteCallback = cb;
  m_sendCompleteCallback = send_cb;
  m_messageCompleteCallback = message_cb;
}
uint32_t ip_to_node_id(Ipv4Address ip) {
  return (ip.Get() >> 8) & 0xffff;
}
uint32_t RdmaHw::GetNicIdxOfQp(Ptr<RdmaQueuePair> qp) {
  uint32_t src = qp->m_src;
  uint32_t dst = qp->m_dest;
  if (src / m_gpus_per_server == dst / m_gpus_per_server ||
      m_rtTable_nxthop_nvswitch.count(qp->dip.Get()) !=
          0) { // src and dst are in the same server, communicate through
               // nvswitch
    auto& v = m_rtTable_nxthop_nvswitch[qp->dip.Get()];
    if (v.size() > 0) {
      return v[qp->GetHash() % v.size()];
    } else {
      NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
    }
  } else { // src and dst don't in the same server, communicate through swicth
    auto& v = m_rtTable[qp->dip.Get()];
    if (v.size() > 0) {
      return v[qp->GetHash() % v.size()];
    } else {
      NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
    }
  }
}
uint64_t RdmaHw::GetQpKey(uint32_t dip, uint16_t sport, uint16_t pg) {
  return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)pg;
}
Ptr<RdmaQueuePair> RdmaHw::GetQp(uint32_t dip, uint16_t sport, uint16_t pg) {
  uint64_t key = GetQpKey(dip, sport, pg);
  auto it = m_qpMap.find(key);
  if (it != m_qpMap.end())
    return it->second;
  return NULL;
}
Ptr<RdmaQueuePair> RdmaHw::AddQueuePair(
    uint32_t src,
    uint32_t dest,
    uint64_t tag,
    uint64_t size,
    uint16_t pg,
    Ipv4Address sip,
    Ipv4Address dip,
    uint16_t sport,
    uint16_t dport,
    uint32_t win,
    uint64_t baseRtt,
    Callback<void> notifyAppFinish,
    Callback<void> notifyAppSent) {
  // create qp
  Ptr<RdmaQueuePair> qp =
      CreateObject<RdmaQueuePair>(pg, sip, dip, sport, dport);
  qp->SetSrc(src);
  qp->SetDest(dest);
  qp->SetTag(tag);
  qp->SetWin(win);
  qp->SetBaseRtt(baseRtt);
  qp->SetVarWin(m_var_win);
  
  if(size != 0){
    qp->PushMessage(size, notifyAppFinish, notifyAppSent);
  }
  // add qp
  uint32_t nic_idx = GetNicIdxOfQp(qp);

  // std::cout << "src is: " << src << ", dst is: " << dest <<  ", nic_idx: " <<
  // nic_idx << ", and the m_nic size is: " << m_nic.size() << std::endl; Assign
  // the qp to specific qbbnetdevice
  m_nic[nic_idx].qpGrp->AddQp(qp);
  uint64_t key = GetQpKey(dip.Get(), sport, pg);
  m_qpMap[key] = qp;
  qp_cnp[key] = 0;
  last_qp_cnp[key] = 0;
  last_qp_rate[key] = 0;

  // set init variables
  DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
  qp->m_rate = m_bps;
  qp->m_max_rate = m_bps;
  
  Ptr<Packet> mtuPacket = GenDataPacket(qp, m_mtu);
  qp->m_tokenBucket.Init(
      mtuPacket->GetSize(),
      Simulator::Now(),
      std::max(mtuPacket->GetSize(), m_nic[nic_idx].dev->m_rdmaEQ->m_token_per_round) * 2);
  
  TypeId congTypeId;
  switch (m_cc_mode) {
    case 1:
      congTypeId = MellanoxDcqcn::GetTypeId();
      break;
    case 3:
      congTypeId = Hpcc::GetTypeId();
      break;
    case 7:
      congTypeId = Timely::GetTypeId();
      break;
    case 8:
      congTypeId = Dctcp::GetTypeId();
      break;
    case 10:
      congTypeId = HpccPint::GetTypeId();
      break;
    case 11:
      congTypeId = Swift::GetTypeId();
      break;
    case 12:
      congTypeId = RealDcqcn::GetTypeId();
      break;
    case 13:
      congTypeId = HpccEcn::GetTypeId();
      break;
    default:
      NS_FATAL_ERROR(
          "cc_mode" << m_cc_mode << " can not match any CongestionTypeId");
      break;
  }
  bool createAlgo = false;
  if (m_nic_coalesce_method == NicCoalesceMethod::PER_QP) {
    ObjectFactory congestionAlgorithmFactory;
    congestionAlgorithmFactory.SetTypeId(congTypeId);
    Ptr<RdmaCongestionOps> algo =
        congestionAlgorithmFactory.Create<RdmaCongestionOps>();
    qp->m_congestionControl = algo;
    createAlgo = true;
  } else if (m_nic_coalesce_method == NicCoalesceMethod::PER_IP) {
    auto& mp = m_nic_peripTable[nic_idx];
    if (mp.find(dip.Get()) == mp.end()) {
      // Create new tableEntry
      mp[dip.Get()] = CreateObject<NicPerIpTableEntry>();

      ObjectFactory congestionAlgorithmFactory;
      congestionAlgorithmFactory.SetTypeId(congTypeId);
      Ptr<RdmaCongestionOps> algo =
          congestionAlgorithmFactory.Create<RdmaCongestionOps>();
      mp[dip.Get()]->m_congestionControl = algo;
      createAlgo = true;
    }
    qp->m_congestionControl = mp[dip.Get()]->m_congestionControl;
    mp[dip.Get()]->qpNum++;
  }
  if(createAlgo){
    qp->m_congestionControl->SetHw(this, m_nic[nic_idx].dev);
    qp->m_congestionControl->LazyInit(qp, m_bps);
    if(!m_cc_configs.empty()){
      for(auto& cc_config : m_cc_configs){
        qp->m_congestionControl->SetAttribute(cc_config.first, *(cc_config.second));
      }
      m_cc_configs.clear();
    }
  }

  // NVLS settings
  if (nvls_enable == 1)
    qp->nvls_enable = 1;
  else
    qp->nvls_enable = 0;
  // Notify Nic
  m_nic[nic_idx].dev->NewQp(qp);

  return qp;
}

void RdmaHw::DeleteQueuePair(Ptr<RdmaQueuePair> qp) {
  // remove qp from the m_qpMap
  uint64_t key = GetQpKey(qp->dip.Get(), qp->sport, qp->m_pg);
  m_qpMap.erase(key);
  qp_cnp.erase(key);
  last_qp_cnp.erase(key);
  last_qp_rate.erase(key);
}

Ptr<RdmaRxQueuePair> RdmaHw::GetRxQp(
    uint32_t sip,
    uint32_t dip,
    uint16_t sport,
    uint16_t dport,
    uint16_t pg,
    bool create) {
  uint64_t key = ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | (uint64_t)dport;
  #ifdef NS3_MTP
  MtpInterface::explicitCriticalSection cs;
  #endif
  auto it = m_rxQpMap.find(key);
  if (it != m_rxQpMap.end()){
	#ifdef NS3_MTP
	cs.ExitSection();
	#endif
	return it->second;
  }
  if (create) {
    // create new rx qp
    Ptr<RdmaRxQueuePair> q = CreateObject<RdmaRxQueuePair>();
    // init the qp
    q->sip = sip;
    q->dip = dip;
    q->sport = sport;
    q->dport = dport;
    q->m_ecn_source.qIndex = pg;
    // store in map
    m_rxQpMap[key] = q;
	#ifdef NS3_MTP
	cs.ExitSection();
	#endif
    return q;
  }
  #ifdef NS3_MTP
  cs.ExitSection();
  #endif
  return NULL;
}

uint32_t RdmaHw::GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q) {
  // BUG就出现在这里了，首先要判断m_rtTable[q->dip]是否存在，若不存在就去判断m_rtTable_nxthop_nvswitch是否存在，如果都不存在，那么就输出错误
  // auto &v = m_rtTable[q->dip];

  if (m_rtTable.count(q->dip) != 0) {
    auto& v = m_rtTable[q->dip];
    if (v.size() > 0)
      return v[q->GetHash() % v.size()];
    else
      NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
  } else if (m_rtTable_nxthop_nvswitch.count(q->dip) != 0) {
    auto& v = m_rtTable_nxthop_nvswitch[q->dip];
    if (v.size() > 0)
      return v[q->GetHash() % v.size()];
    else
      NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
  } else {
    NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
  }
  NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
}
void RdmaHw::DeleteRxQp(uint32_t dip, uint16_t pg, uint16_t dport) {
  uint64_t key = ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | (uint64_t)dport;
  m_rxQpMap.erase(key);
}

int RdmaHw::SendPacketComplete(Ptr<Packet> p, CustomHeader& ch) {
  uint16_t qIndex = ch.udp.pg;
  uint16_t port = ch.udp.sport;
  uint32_t seq = ch.udp.seq;
  // uint8_t cnp = (ch.flags >> qbbHeader::FLAG_CNP) & 1;
  // int i;
  uint64_t key = GetQpKey(ch.dip, port, qIndex);
  Ptr<RdmaQueuePair> qp = m_qpMap.find(key) == m_qpMap.end() ? nullptr : m_qpMap[key];
  // std::cout << "处理发送回调, src: " << qp->m_src << " to dst: " <<
  // qp->m_dest << std::endl; std::cout << "处理发送回调, src: " << qp->m_src <<
  // " to dst: " << qp->m_dest << "\n";
  if (qp == NULL) {
    return 0;
  }
  // uint32_t nic_idx = GetNicIdxOfQp(qp);
  // Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
  SendComplete(qp);
}

void RdmaHw::SendComplete(Ptr<RdmaQueuePair> qp) {
  NS_ASSERT(!m_sendCompleteCallback.IsNull());

  m_sendCompleteCallback(qp);
}

int RdmaHw::ReceiveUdp(Ptr<Packet> p, CustomHeader& ch) {
  uint8_t ecnbits = ch.GetIpv4EcnBits();

  uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();
  // TODO find corresponding rx queue pair
  Ptr<RdmaRxQueuePair> rxQp =
      GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, true);
  if (ecnbits != 0) {
    rxQp->m_ecn_source.ecnbits |= ecnbits;
    rxQp->m_ecn_source.qfb++;
  }
  rxQp->m_ecn_source.total++;
  rxQp->m_milestone_rx = m_ack_interval;

  int x = ReceiverCheckSeq(ch.udp.seq, rxQp, payload_size);
  if (x == 1 || x == 2) { // generate ACK or NACK
    qbbHeader seqh;
    seqh.SetSeq(rxQp->ReceiverNextExpectedSeq);
    seqh.SetPG(ch.udp.pg);
    seqh.SetSport(ch.udp.dport);
    seqh.SetDport(ch.udp.sport);
    seqh.SetIntHeader(ch.udp.ih);
    if (ecnbits) {
      if (m_cc_mode == 1) {
        seqh.SetCnp();
      } else {
        uint64_t key = 0;
        if (m_nic_coalesce_method == NicCoalesceMethod::PER_IP) {
          key = GetNicIdxOfRxQp(rxQp); // each nic has a timer
        } else if (m_nic_coalesce_method == NicCoalesceMethod::PER_QP) {
          key = GetQpKey(
              ch.sip, ch.udp.sport, ch.udp.pg); // each rxqp has a timer
        }
        if (m_cnpTable.count(key) == 0) {
          // Create new tableEntry
          m_cnpTable[key] = Simulator::Now() - m_cnp_interval;
        }
        if (Simulator::Now() - m_cnpTable[key] >= m_cnp_interval) {
          seqh.SetCnp();
          m_cnpTable[key] = Simulator::Now();
        }
      }
    }

    Ptr<Packet> newp = Create<Packet>(
        std::max(60 - 14 - 20 - (int)seqh.GetSerializedSize(), 0));
    newp->AddHeader(seqh);

    Ipv4Header head; // Prepare IPv4 header
    head.SetDestination(Ipv4Address(ch.sip));
    head.SetSource(Ipv4Address(ch.dip));
    head.SetProtocol(x == 1 ? 0xFC : 0xFD); // ack=0xFC nack=0xFD
    head.SetTtl(64);
    head.SetPayloadSize(newp->GetSize());
    head.SetIdentification(rxQp->m_ipid++);
    // GPU receives the packet and generate ACK with NVLS tag
    if (ch.m_tos == 4)
      head.SetTos(4);

    newp->AddHeader(head);
    AddHeader(newp, 0x800); // Attach PPP header
    uint32_t sip = ch.sip;
    uint32_t sid = (sip >> 8) & 0xffff;
    uint32_t dip = ch.dip;
    uint32_t did = (dip >> 8) & 0xffff;
    // send
    uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
    m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
    // 发送给目标 NVSwitch 的报文
    if (did == m_node->GetId() && m_node->GetNodeType() == 2 && ch.m_tos == 4)
      m_nic[nic_idx].dev->SwitchAsHostSend();
    else
      m_nic[nic_idx].dev->TriggerTransmit();
  }
  return 0;
}

int RdmaHw::ReceiveCnp(Ptr<Packet> p, CustomHeader& ch) {
  // QCN on NIC
  // This is a Congestion signal
  // Then, extract data from the congestion packet.
  // We assume, without verify, the packet is destinated to me
  uint32_t qIndex = ch.cnp.qIndex;
  if (qIndex == 1) { // DCTCP
    return 0;
  }
  uint16_t udpport = ch.cnp.fid; // corresponds to the sport
  uint8_t ecnbits = ch.cnp.ecnBits;
  uint16_t qfb = ch.cnp.qfb;
  uint16_t total = ch.cnp.total;

  uint32_t i;
  // get qp
  Ptr<RdmaQueuePair> qp = GetQp(ch.sip, udpport, qIndex);
  if (qp == NULL)
    std::cout << "ERROR: QCN NIC cannot find the flow\n";
  // get nic
  uint32_t nic_idx = GetNicIdxOfQp(qp);
  Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

  if (qp->m_rate == 0) // lazy initialization
  {
    qp->m_congestionControl->LazyInit(qp, dev->GetDataRate());
  }
  return 0;
}

int RdmaHw::ReceiveAck(Ptr<Packet> p, CustomHeader& ch) {
  uint16_t qIndex = ch.ack.pg;
  uint16_t port = ch.ack.dport;
  uint64_t seq = ch.ack.seq;
  uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;

  int i;
  Ptr<RdmaQueuePair> qp = GetQp(ch.sip, port, qIndex);
  if (qp == NULL) {
    return 0;
  }

  uint32_t nic_idx = GetNicIdxOfQp(qp);
  Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
  if (m_ack_interval == 0)
    std::cout << "ERROR: shouldn't receive ack\n";
  else {
    if (!m_backto0) {
      qp->Acknowledge(seq);
    } else {
      uint64_t goback_seq = seq / m_chunk * m_chunk;
      qp->Acknowledge(goback_seq);
    }
    if (qp->IsCurMessageFinished()) {
      QpCompleteMessage(qp);
      // if (qp->IsFinished()) {
      //   QpComplete(qp);
      // }
      return 0;
    }
  }
  if (ch.l3Prot == 0xFD) // NACK
    RecoverQueue(qp);

  // handle cnp
  if (cnp) {
    uint64_t key = GetQpKey(qp->dip.Get(), qp->sport, qp->m_pg);
    qp_cnp[key]++; // update for the number of cnp this qp has received
  }

  qp->m_congestionControl->HandleAck(qp, p, ch);

  uint32_t sip = ch.sip;
  uint32_t sid = (sip >> 8) & 0xffff;
  uint32_t dip = ch.dip;
  uint32_t did = (dip >> 8) & 0xffff;
  // ACK may advance the on-the-fly window, allowing more packets to send
  if (did == m_node->GetId() && m_node->GetNodeType() == 2)
    dev->SwitchAsHostSend();
  else
    dev->TriggerTransmit();
  // std:://cout << "ack triggere transmitted\n";
  return 0;
}

int RdmaHw::Receive(Ptr<Packet> p, CustomHeader& ch) {
  if (ch.l3Prot == 0x11) { // UDP
    ReceiveUdp(p, ch);
  } else if (ch.l3Prot == 0xFF) { // CNP
    ReceiveCnp(p, ch);
  } else if (ch.l3Prot == 0xFD) { // NACK
    ReceiveAck(p, ch);
  } else if (ch.l3Prot == 0xFC) { // ACK
    ReceiveAck(p, ch);
  }
  return 0;
}

int RdmaHw::ReceiverCheckSeq(
    uint64_t seq,
    Ptr<RdmaRxQueuePair> q,
    uint32_t size) {
  uint64_t expected = q->ReceiverNextExpectedSeq;
  if (seq == expected) {
    q->ReceiverNextExpectedSeq = expected + size;
    if (q->ReceiverNextExpectedSeq >= q->m_milestone_rx) {
      q->m_milestone_rx += m_ack_interval;
      return 1; // Generate ACK
    } else if (q->ReceiverNextExpectedSeq % m_chunk == 0) {
      return 1;
    } else {
      return 5;
    }
  } else if (seq > expected) {
    // Generate NACK
    if (Simulator::Now() >= q->m_nackTimer || q->m_lastNACK != expected) {
      q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
      q->m_lastNACK = expected;
      if (m_backto0) {
        q->ReceiverNextExpectedSeq =
            q->ReceiverNextExpectedSeq / m_chunk * m_chunk;
      }
      return 2;
    } else
      return 4;
  } else {
    // Duplicate.
    return 3;
  }
}
void RdmaHw::AddHeader(Ptr<Packet> p, uint16_t protocolNumber) {
  PppHeader ppp;
  ppp.SetProtocol(EtherToPpp(protocolNumber));
  p->AddHeader(ppp);
}
uint16_t RdmaHw::EtherToPpp(uint16_t proto) {
  switch (proto) {
    case 0x0800:
      return 0x0021; // IPv4
    case 0x86DD:
      return 0x0057; // IPv6
    default:
      NS_ASSERT_MSG(false, "PPP Protocol number not defined!");
  }
  return 0;
}

void RdmaHw::RecoverQueue(Ptr<RdmaQueuePair> qp) {
  qp->snd_nxt = qp->snd_una;
}

void RdmaHw::QpComplete(Ptr<RdmaQueuePair> qp) {
  NS_ASSERT(!m_qpCompleteCallback.IsNull());
  

  uint32_t nic_idx = GetNicIdxOfQp(qp);
  #ifdef NS3_MTP
          MtpInterface::explicitCriticalSection cs;
  #endif
          m_nic[nic_idx].dev->m_rdmaEQ->RemoveFinishedQps();
  #ifdef NS3_MTP
          cs.ExitSection();
  #endif
  
  qp->m_congestionControl->QpComplete(qp);
  if(m_nic_coalesce_method == NicCoalesceMethod::PER_QP)
  {
    qp->m_congestionControl->AllComplete();
  }
  else if(m_nic_coalesce_method == NicCoalesceMethod::PER_IP)
  {
    m_nic_peripTable[nic_idx][qp->dip.Get()]->qpNum--;
    if(m_nic_peripTable[nic_idx][qp->dip.Get()]->qpNum == 0)
    {
      // No qp exists
      // std::cout<<"No qp exists, so delete the ip "<<qp->dip.Get()<<" from nic "<<nic_idx<<std::endl;
      m_nic_peripTable[nic_idx][qp->dip.Get()]->m_congestionControl->AllComplete();
      m_nic_peripTable[nic_idx].erase(qp->dip.Get());
    }
    
  }

  // This callback will log info
  // It may also delete the rxQp on the receiver
  m_qpCompleteCallback(qp);

  qp->m_congestionControl = nullptr;

  // delete the qp
  DeleteQueuePair(qp);
}

void RdmaHw::QpCompleteMessage(Ptr<RdmaQueuePair> qp) {
  // qp->m_messages.front().m_notifyAppFinish();
  // callback
  RdmaQueuePair::RdmaMessage msg = qp->m_messages.front();
  qp->FinishMessage();
  m_messageCompleteCallback(qp, msg.m_size);
  if(!qp->m_messages.empty()){
    // Have more messages to send
    // std::cout<<"at "<<Simulator::Now().GetTimeStep()<<"ns, qp src:"<<qp->m_src<<" dst:"<<qp->m_dest<<" port:"<<qp->sport<<" dport:"<<qp->dport<<" has more messages to send\n";
    qp->m_messages.front().m_startSeq = qp->snd_nxt;
    uint32_t nic_idx = GetNicIdxOfQp(qp);
    m_nic[nic_idx].dev->TriggerTransmit();
  }
}

void RdmaHw::SetLinkDown(Ptr<QbbNetDevice> dev) {
  printf("RdmaHw: node:%u a link down\n", m_node->GetId());
}

void RdmaHw::AddTableEntry(
    Ipv4Address& dstAddr,
    uint32_t intf_idx,
    bool is_nvswitch) {
  uint32_t dip = dstAddr.Get();
  if (is_nvswitch == false)
    m_rtTable[dip].push_back(intf_idx);
  else {
    m_rtTable_nxthop_nvswitch[dip].push_back(intf_idx);
  }
}

void RdmaHw::ClearTable() {
  m_rtTable.clear();
  m_rtTable_nxthop_nvswitch.clear();
}

void RdmaHw::RedistributeQp() {
  // clear old qpGrp
  for (uint32_t i = 0; i < m_nic.size(); i++) {
    if (m_nic[i].dev == NULL)
      continue;
    m_nic[i].qpGrp->Clear();
  }

  // redistribute qp
  for (auto& it : m_qpMap) {
    Ptr<RdmaQueuePair> qp = it.second;
    uint32_t nic_idx = GetNicIdxOfQp(qp);
    m_nic[nic_idx].qpGrp->AddQp(qp);
    // Notify Nic
    m_nic[nic_idx].dev->ReassignedQp(qp);
  }
}

Ptr<Packet> RdmaHw::GetNxtPacket(Ptr<RdmaQueuePair> qp) {
  uint64_t payload_size = qp->GetBytesLeft();
  if ((uint64_t)m_mtu < payload_size)
    payload_size = m_mtu;
  Ptr<Packet> p = GenDataPacket(qp, payload_size);
  // update state
  qp->snd_nxt += payload_size;
  // std::cout << "current snd_nxt is: " << qp->snd_nxt << ", the window is: "
  // << qp->m_win << std::endl;
  qp->m_ipid++;

  // return
  return p;
}

Ptr<Packet> RdmaHw::GenDataPacket(Ptr<RdmaQueuePair> qp, uint32_t pkt_size) {
  Ptr<Packet> p = Create<Packet>(pkt_size);
  // add SimpleSeqTsHeader
  SimpleSeqTsHeader seqTs;
  seqTs.SetSeq(qp->snd_nxt);
  seqTs.SetPG(qp->m_pg);
  p->AddHeader(seqTs);
  // add udp header
  UdpHeader udpHeader;
  udpHeader.SetDestinationPort(qp->dport);
  udpHeader.SetSourcePort(qp->sport);
  p->AddHeader(udpHeader);
  // add ipv4 header
  Ipv4Header ipHeader;
  ipHeader.SetSource(qp->sip);
  ipHeader.SetDestination(qp->dip);
  ipHeader.SetProtocol(0x11);
  ipHeader.SetPayloadSize(p->GetSize());
  ipHeader.SetTtl(64);
  // nvls <-> ToS, ToS = 1 -> NVLS enable
  if (qp->nvls_enable == 1)
    ipHeader.SetTos(4);
  else
    ipHeader.SetTos(0);
  ipHeader.SetIdentification(qp->m_ipid);
  p->AddHeader(ipHeader);
  // add ppp header
  PppHeader ppp;
  ppp.SetProtocol(
      0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
  p->AddHeader(ppp);
  return p;
}

void RdmaHw::PktSent(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> pkt,
    Time interframeGap) {
  qp->lastPktSize = pkt->GetSize();
  qp->m_congestionControl->PktSent(qp, pkt);
  // DataRate old = qp->m_rate;
  qp->m_rate = qp->m_congestionControl->m_ccRate;
  // if(old != qp->m_rate)
  //   std::cout<<"At "<<Simulator::Now().GetTimeStep()<<" qp "<<qp->m_src<<" "<<qp->m_dest<<" "<<qp->sport<<" "<<qp->dport<<" rate is "<<qp->m_rate.GetBitRate()<<" old rate is "<<old.GetBitRate()<<std::endl;
  UpdateNextAvail(qp, interframeGap, pkt->GetSize());
}

void RdmaHw::UpdateNextAvail(
    Ptr<RdmaQueuePair> qp,
    Time interframeGap,
    uint32_t pkt_size) {
  Time sendingTime;
  if (m_rateBound)
    sendingTime = interframeGap + qp->m_rate.CalculateBytesTxTime(pkt_size);
  else
    sendingTime = interframeGap + qp->m_max_rate.CalculateBytesTxTime(pkt_size);
  qp->m_nextAvail = Simulator::Now() + sendingTime;
}

void RdmaHw::ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate) {
#if 1
  Time sendingTime = qp->m_rate.CalculateBytesTxTime(qp->lastPktSize);
  Time new_sendintTime = new_rate.CalculateBytesTxTime(qp->lastPktSize);
  qp->m_nextAvail = qp->m_nextAvail + new_sendintTime - sendingTime;
  // update nic's next avail event
  uint32_t nic_idx = GetNicIdxOfQp(qp);
  m_nic[nic_idx].dev->UpdateNextAvail(qp->m_nextAvail);
#endif

  // change to new rate
  qp->m_rate = new_rate;
}
/**
 * when nic send a packet, update the bytes it has sent
 */
void RdmaHw::UpdateTxBytes(uint32_t port_id, uint64_t bytes) {
  tx_bytes[port_id] += bytes;
}
/**
 * when nic receive a packet, update the bytes it has sent
 */
void RdmaHw::UpdateRxBytes(uint32_t port_id, uint64_t bytes) {
  rx_bytes[port_id] += bytes;
}
/**
 * output format:
 * time, host_id, port_id, bandwidth
 */
void RdmaHw::PrintHostTxBW(FILE* bw_output, uint32_t bw_mon_interval) {
  for (int i = 0; i < m_nic.size(); ++i) {
    if (tx_bytes[i] == last_tx_bytes[i]) {
      continue;
    }
    double bw =
        (tx_bytes[i] - last_tx_bytes[i]) * 8 * 1e6 / (bw_mon_interval); // bit/s
    bw = bw * 1.0 / 1e9; // Gbps
    fprintf(
        bw_output,
        "%lu, %u, %u, %f\n",
        Simulator::Now().GetTimeStep(),
        m_node->GetId(),
        i,
        bw);
    fflush(bw_output);
    last_tx_bytes[i] = tx_bytes[i];
  }
}
/**
 * output format:
 * time, host_id, port_id, bandwidth
 */
void RdmaHw::PrintHostRxBW(FILE* bw_output, uint32_t bw_mon_interval) {
  for (int i = 0; i < m_nic.size(); ++i) {
    if (rx_bytes[i] == last_rx_bytes[i]) {
      continue;
    }
    double bw =
        (rx_bytes[i] - last_rx_bytes[i]) * 8 * 1e6 / (bw_mon_interval); // bit/s
    bw = bw * 1.0 / 1e9; // Gbps
    fprintf(
        bw_output,
        "%lu, %u, %u, %f\n",
        Simulator::Now().GetTimeStep(),
        m_node->GetId(),
        i,
        bw);
    fflush(bw_output);
    last_rx_bytes[i] = rx_bytes[i];
  }
}
/**
 * output format:
 * time, src, dst, sport, dport, size, rate
 */
void RdmaHw::PrintQPRate(FILE* rate_output) {
  std::unordered_map<uint64_t, Ptr<RdmaQueuePair>>::iterator it =
      m_qpMap.begin();
  for (; it != m_qpMap.end(); it++) {
    Ptr<RdmaQueuePair> qp = it->second;
    uint64_t key = it->first;
    // if (qp->m_rate.GetBitRate() == last_qp_rate[key]) {
    //   continue;
    // }
    fprintf(
        rate_output,
        "%lu, %u, %u, %u, %u, %lu, %lu\n",
        Simulator::Now().GetTimeStep(),
        qp->m_src,
        qp->m_dest,
        qp->sport,
        qp->dport,
        qp->snd_nxt - qp->m_messages.front().m_startSeq,
        qp->m_rate.GetBitRate());
    fflush(rate_output);
    last_qp_rate[key] = qp->m_rate.GetBitRate();
  }
}
/**
 * output format:
 * time, src, dst, sport, dport, size, cnp_number
 */
void RdmaHw::PrintQPCnpNumber(FILE* cnp_output) {
  std::unordered_map<uint64_t, Ptr<RdmaQueuePair>>::iterator it =
      m_qpMap.begin();
  for (; it != m_qpMap.end(); it++) {
    Ptr<RdmaQueuePair> qp = it->second;
    uint64_t key = it->first;
    if (qp_cnp[key] != last_qp_cnp[key]) {
      fprintf(
          cnp_output,
          "%lu, %u, %u, %u, %u, %u, %u\n",
          Simulator::Now().GetTimeStep(),
          qp->m_src,
          qp->m_dest,
          qp->sport,
          qp->dport,
          qp->m_messages.front().m_size,
          qp_cnp[key]);
      fflush(cnp_output);
      last_qp_cnp[key] = qp_cnp[key];
    }
  }
}
// void RdmaHw::SetPintSmplThresh(double p){
//        pint_smpl_thresh = (uint32_t)(65536 * p);
// }
} // namespace ns3
