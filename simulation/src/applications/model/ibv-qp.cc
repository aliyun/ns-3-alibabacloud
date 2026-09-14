#include "ibv-qp.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/rdma-driver.h"
#include "ns3/node-list.h"
#include <chrono>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("IbvQP");
NS_OBJECT_ENSURE_REGISTERED(IbvQP);
IbvQP::IbvQP() : 
  m_qp(NULL),
  m_is_valid(true),
  m_is_rts(false),
  m_qp_state(IBV_QPS_RESET),
  m_src_port(10000),
  m_dst_port(10000),
  m_enable_gpu_p2p(false),
  m_pg(3),
  is_type_confirmed(false),
  total_post_write_bytes(0),
  total_post_write_imm_bytes(0),
  total_comp_write_bytes(0),
  total_comp_write_imm_bytes(0),
  qp_use_propose(QpUnknown)
{
}

TypeId IbvQP::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::IbvQP")
    .SetParent<Object>()
    .AddConstructor<IbvQP>();
  return tid;
}

void IbvQP::SetSendCQ(Ptr<IbvCQ> cq)
{
  m_send_cq = cq;
}

void IbvQP::SetRecvCQ(Ptr<IbvCQ> cq)
{
  m_recv_cq = cq;
}

void IbvQP::Nop(std::string op_name)
{
  NS_LOG_DEBUG("IbvQP::Nop op_name = " << op_name);
}

uint32_t IbvQP::CalculateWindow(uint32_t bdp)
{
  return bdp;
}

void IbvQP::PrintQpState()
{
  if(m_qp->m_messages.size()!=0 || m_qp->GetBytesLeft()!=0)
    NS_LOG_LOGIC(GetDebugName() << " MsgQueueSize: " << m_qp->m_messages.size() << ", BytesLeft: " << m_qp->GetBytesLeft()
                << ", snd_nxt: " << m_qp->snd_nxt << ", snd_una: " << m_qp->snd_una << ", Inflight: " << m_qp->snd_nxt - m_qp->snd_una
                << ", mFrontLength: " << m_qp->m_messages.front().send_wr.total_length << ", mFrontStartSeq: " << m_qp->m_messages.front().m_startSeq
                << " sum = " << m_qp->m_messages.front().send_wr.total_length+m_qp->m_messages.front().m_startSeq);
  Simulator::Schedule(NanoSeconds(2000000), &IbvQP::PrintQpState, this);
}

void IbvQP::EnqueueSendRequest(shm_ibv_send_wr send_wr)
{
  // 确定QP用途类型
  if(!is_type_confirmed) {
    
    if(send_wr.is_post_fifo) {
      is_type_confirmed = true;
      qp_use_propose = QpPostFifo;
    } else if(send_wr.opcode==IBV_WR_RDMA_WRITE_WITH_IMM) {
      is_type_confirmed = true;
      qp_use_propose = QpSendData;      
    } else if(send_wr.opcode==IBV_WR_RDMA_READ) {
      is_type_confirmed = true;
      qp_use_propose = QpFlushRead;
    }
  }
  // 记录下发的请求的字节数
  if(qp_use_propose==QpSendData) total_post_write_imm_bytes += send_wr.total_length;
  if(qp_use_propose==QpPostFifo) total_post_write_bytes += send_wr.total_length;
  // 执行下发的请求/入队列
  if ((send_wr.opcode==IBV_WR_RDMA_WRITE) || (send_wr.opcode==IBV_WR_RDMA_WRITE_WITH_IMM)) {
    std::string line = std::to_string(Simulator::Now().GetNanoSeconds()) + " EnqueueSendRequest " + std::to_string(send_wr.total_length) + " with WrId = " + std::to_string(send_wr.wr_id);
    lines.push_back(line);
    
    // NS_LOG_LOGIC(GetDebugName() << (send_wr.opcode==IBV_WR_RDMA_WRITE ? " IBV_WR_RDMA_WRITE " : " IBV_WR_RDMA_WRITE_WITH_IMM ") << line);
    if(m_node->GetId() / 8 ==m_remote_node->GetId() / 8) {
      // 机内链路
      // Simulator::Schedule(NanoSeconds(send_wr.total_length * 8 / 1320), &IbvQP::WrCompleted, this, m_qp, send_wr);
      m_qp->PushMessage(send_wr, MakeCallback(&IbvQP::WrCompleted, this), MakeCallback(&IbvQP::WrSent, this));
      Ptr<RdmaDriver> rdma = m_node->GetObject<RdmaDriver>();
      Ptr<ns3::QbbNetDevice> nic = rdma->m_rdma->GetNicOfQp(m_qp);
      nic->TriggerTransmit();
    } else {
      // 跨机链路
      m_qp->PushMessage(send_wr, MakeCallback(&IbvQP::WrCompleted, this), MakeCallback(&IbvQP::WrSent, this));
      Ptr<RdmaDriver> rdma = m_node->GetObject<RdmaDriver>();
      Ptr<ns3::QbbNetDevice> nic = rdma->m_rdma->GetNicOfQp(m_qp);
      nic->TriggerTransmit();
    }

  } else if (send_wr.opcode==IBV_WR_RDMA_READ) {
    // 在NCCL中的READ请求 只有在ncclIbIflush中调用 是自己读自己 所以直接生成CQE
    m_send_cq->GenerateSendCQE(send_wr,m_qp_num,m_dst_qp_num);
  }

}

void IbvQP::EnqueueRecvRequest(shm_ibv_recv_wr recv_wr)
{
  if(!is_type_confirmed) {
    is_type_confirmed = true;
    qp_use_propose = QpRecvData;
  }
  std::string line = std::to_string(Simulator::Now().GetNanoSeconds()) + " EnqueueRecvRequest";
  lines.push_back(line);
  m_waiting_recv_wrs.push(recv_wr);
}

void IbvQP::ConsumeRecvWrAndGenerateRecvCQE(shm_ibv_send_wr send_wr)
{
  if (send_wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM) {
    if (m_waiting_recv_wrs.empty()) {
      NS_LOG_ERROR("No Recv Wr for RDMA_WRITE_WITH_IMM");
      NS_FATAL_ERROR("No Recv Wr for RDMA_WRITE_WITH_IMM");
    }
    // 消耗一个recv wr
    shm_ibv_recv_wr recv_wr = m_waiting_recv_wrs.front();
    m_waiting_recv_wrs.pop();
    // 生成一个CQE放到recv CQ中
    m_recv_cq->GenerateRecvCQE(recv_wr,m_qp_num,m_dst_qp_num,send_wr.imm_data,true);
  } else {
    NS_LOG_WARN("Unsupported opcode: " << send_wr.opcode);
  }
}

void IbvQP::WrSent(Ptr<RdmaQueuePair> qp, shm_ibv_send_wr wr)
{
}

void IbvQP::WrCompleted(Ptr<RdmaQueuePair> qp, shm_ibv_send_wr wr)
{
  // NS_LOG_LOGIC(GetDebugName() << " WrCompleted " << wr.total_length << " with WrId = " << wr.wr_id);
  // 如果是PostFifo的数据 需要把真数据Write到对方的进程空间
  if(wr.is_post_fifo) {
    struct ncclIbSendFifo send_fifo;
    send_fifo.addr = wr.post_fifo_addr;
    send_fifo.size = wr.post_fifo_size;
    send_fifo.rkeys[0] = wr.post_fifo_rkeys[0];
    send_fifo.rkeys[1] = wr.post_fifo_rkeys[1];
    send_fifo.nreqs = wr.post_fifo_nreqs;
    send_fifo.tag = wr.post_fifo_tag;
    send_fifo.idx = wr.post_fifo_idx;
    send_fifo.wr_id = wr.post_fifo_wr_id;
    m_ibv_interface->ExecPostFifo(send_fifo, m_remote_node->GetId(), wr.remote_addr);
  }
  if ((wr.send_flags & IBV_SEND_SIGNALED) == IBV_SEND_SIGNALED) {
    if(wr.opcode==IBV_WR_RDMA_WRITE) {
      std::string line = std::to_string(Simulator::Now().GetNanoSeconds()) + " Write Complete " + std::to_string(wr.total_length) + " with WrId = " + std::to_string(wr.wr_id);
      lines.push_back(line);
      m_send_cq->GenerateSendCQE(wr,m_qp_num,m_dst_qp_num);
    } else if (wr.opcode==IBV_WR_RDMA_WRITE_WITH_IMM) {
      std::string line = std::to_string(Simulator::Now().GetNanoSeconds()) + " WriteWithImm Complete " + std::to_string(wr.total_length) + " with WrId = " + std::to_string(wr.wr_id);
      lines.push_back(line);
      m_send_cq->GenerateSendCQE(wr,m_qp_num,m_dst_qp_num);
      // 让对端QP的做处理 具体处理逻辑包括: 先消耗一个RecvWR 然后生成一个CQE到recvCQ
      m_remote_qp->ConsumeRecvWrAndGenerateRecvCQE(wr);
    } else if(wr.opcode==IBV_WR_RDMA_READ) {
      m_send_cq->GenerateSendCQE(wr,m_qp_num,m_dst_qp_num);
    } else {
      NS_LOG_ERROR("Unsupported opcode: " << wr.opcode);
      NS_FATAL_ERROR("Unsupported opcode: " << wr.opcode);
    }
    // NS_LOG_LOGIC("WrCompleted Counter " << m_ibv_interface->GetPollCounter() << " " << GetDebugName() << " wr_id=" << wr.wr_id << ", opcode=" << wr.opcode << ", bytes=" << wr.total_length);
  }
}

std::string IbvQP::GetName()
{
  return m_name;
}

std::string IbvQP::GetDebugName()
{
  char info[100];
  sprintf(info, "QP(%u,%u)", m_node->GetId(), m_qp_num);
  return std::string(info);
}

void IbvQP::SetSrcIP(Ipv4Address ip)
{
  m_src_ip = ip;
}

void IbvQP::SetDstIP(Ipv4Address ip)
{
  m_dst_ip = ip;
}

void IbvQP::SetName(std::string name)
{
  m_name = name;
}

Ptr<QbbNetDevice> IbvQP::GetDevice()
{
  return m_device;
}

void IbvQP::SetDevice(Ptr<QbbNetDevice> qbbDevice)
{
  m_device = qbbDevice;
}

void IbvQP::SetQPCap(ibv_qp_cap cap)
{
  m_cap = cap;
}
void IbvQP::SetSqSigAll(int sq_sig_all)
{
  m_sq_sig_all = sq_sig_all;
}
void IbvQP::SetQPNum(uint32_t qp_num)
{
  m_qp_num = qp_num;
}
void IbvQP::SetDstQPNum(uint32_t dst_qp_num)
{
  NS_LOG_INFO("IbvQP::SetDstQPNum: " << m_name << " " << m_dst_qp_num << " -> " << dst_qp_num);
  m_dst_qp_num = dst_qp_num;
}
void IbvQP::SetQPState(enum ibv_qp_state state)
{
  NS_LOG_DEBUG("IbvQP::SetQPState: " << m_name << " " << m_qp_num << " " << m_qp_state << " -> " << state);
  m_qp_state = state;
}

void IbvQP::SetAddressAttr(ibv_ah_attr ah_attr)
{
  m_ah_attr = ah_attr;
  uint32_t remote_node_id = static_cast<uint32_t>((m_ah_attr.grh.dgid.global.interface_id & 0xFFFFFFFF00000000) >> 32);  // remote NodeID 高32位
  uint32_t dip = static_cast<uint32_t>(m_ah_attr.grh.dgid.global.interface_id & 0xFFFFFFFF);  // remote IP 低32位
  m_remote_node = NodeList::GetNode(remote_node_id);
  if (!m_remote_node) {
    NS_LOG_ERROR("Incorrect remote_node_id: Remote Node not found");
    NS_FATAL_ERROR("Incorrect remote_node_id: Remote Node not found");
  }
  m_dst_ip.Set(dip);
  // 获取对端节点的IbvInterface
  // 然后获取对端QP
  Ptr<IbvInterface> remote_interface = DynamicCast<IbvInterface>(m_remote_node->GetApplication(0));
  if(!remote_interface) {
    NS_LOG_ERROR("Remote IbvInterface not found");
    NS_FATAL_ERROR("Remote IbvInterface not found");
  } else {
    NS_LOG_INFO("Remote IbvInterface found");
  }
  m_remote_qp = remote_interface->GetQP(m_dst_qp_num);
  NS_LOG_LOGIC("Connection: " << GetDebugName() << " <==> " << m_remote_qp->GetDebugName());
  NS_ASSERT_MSG(m_remote_qp, "Remote QP not found");
}


void IbvQP::SetPg(uint16_t pg)
{
  m_pg = pg;
}

void IbvQP::SetSrcPort(uint16_t port)
{
  m_src_port = port;
}

void IbvQP::SetDstPort(uint16_t port)
{
  m_dst_port = port;
}

void IbvQP::SetPairBdp(uint32_t bdp)
{
  NS_LOG_FUNCTION("IbvQP::SetPairBdp: " << m_name << " " << bdp);
  m_pair_bdp = bdp;
}

void IbvQP::SetPairRtt(uint64_t rtt)
{
  m_pair_rtt = rtt;
}

void IbvQP::SetGpusPerServer(int num_gpus_per_server)
{
  m_num_gpus_per_server = num_gpus_per_server;
}

void IbvQP::EnableVarWin()
{
  m_enable_var_win = true;
}

void IbvQP::DisableVarWin()
{
  m_enable_var_win = false;
}

void IbvQP::EnableP2P()
{
  m_enable_gpu_p2p = true;
}
void IbvQP::DisableP2P()
{
  m_enable_gpu_p2p = false;
}
void IbvQP::SetNicIdx(uint32_t nic_idx)
{
  m_nic_idx = nic_idx;
}

void IbvQP::SetIbvInterface(Ptr<IbvInterface> ibvInterface)
{
  m_ibv_interface = ibvInterface;
}

Ptr<IbvCQ> IbvQP::GetSendCQ()
{
  return m_send_cq;
}

Ptr<IbvCQ> IbvQP::GetRecvCQ()
{
  return m_recv_cq;
}

ibv_qp_cap IbvQP::GetQPCap()
{
  return m_cap;
}
ibv_qp_type IbvQP::GetQPType()
{
  return m_qp_type;
}
int IbvQP::GetSqSigAll()
{
  return m_sq_sig_all;
}
uint32_t IbvQP::GetQPNum()
{
  return m_qp_num;
}
uint32_t IbvQP::GetDstQPNum()
{
  return m_dst_qp_num;
}
ibv_qp_state IbvQP::GetQPState()
{
  return m_qp_state;
}
ibv_ah_attr IbvQP::GetAddressAttr()
{
  return m_ah_attr;
}

uint16_t IbvQP::GetPg()
{
  return m_pg;
}
uint16_t IbvQP::GetSrcPort()
{
  return m_src_port;
}
uint16_t IbvQP::GetDstPort()
{
  return m_dst_port;
}
uint32_t IbvQP::GetSrcIP()
{
  return m_src_ip.Get();
}
uint32_t IbvQP::GetDstIP()
{
  return m_dst_ip.Get();
}
uint32_t IbvQP::GetPairBdp()
{
  return m_pair_bdp;
}
uint64_t IbvQP::GetPairRtt()
{
  return m_pair_rtt;
}
uint32_t IbvQP::GetNicIndex()
{
  return m_nic_idx;
}
Ptr<RdmaQueuePair> IbvQP::GetRdmaQueuePair()
{
  return m_qp;
}

Ptr<IbvQP> IbvQP::GetRemoteQP()
{
  return m_remote_qp;
}
void IbvQP::SetShmSegment(boost::interprocess::managed_shared_memory *segment)
{
  m_segment = segment;
}
void IbvQP::QPCommInit()
{
  Ptr<RdmaDriver> rdma = m_node->GetObject<RdmaDriver>();
  if(!rdma) {
    NS_LOG_ERROR("No RdmaDriver found");
    NS_FATAL_ERROR("No RdmaDriver found");
  }
  uint64_t tmpTag = 1;
  if(m_node->GetId()==m_remote_node->GetId()) {
    // ncclIbIflush会自己读自己
    return;
  }
  m_qp = rdma->AddQueuePair(m_node->GetId(),m_remote_node->GetId(),tmpTag,0, m_pg, m_src_ip, m_dst_ip, m_src_port,
                        m_dst_port, this->CalculateWindow(m_pair_bdp), m_pair_rtt,
                        MakeCallback(&IbvQP::WrCompleted, this),
                        MakeCallback(&IbvQP::WrSent, this));
  m_qp->SetSrcQpNum(m_qp_num);
  m_qp->SetDstQpNum(m_dst_qp_num);
  NS_LOG_LOGIC("qp(addr=" << GetPointer(m_qp) << ") (" << m_node->GetId() 
                            << "," << m_qp_num << ")" << " set src_port=" << m_src_port << ", dst_port=" << m_dst_port 
                            << " for srcIP=" << m_src_ip.Get() << ", dstIP=" << m_dst_ip.Get());
  // PrintQpState();

  if(!m_qp) {
    NS_LOG_ERROR("Failed to add QP to nic");
    NS_FATAL_ERROR("Failed to add QP to nic");
  }
  m_is_rts = true;
}
uint64_t IbvQP::GetBytesLeft()
{
  if(!m_qp) {
    return 0;
  } else {
    uint64_t bytes_left = m_qp->GetBytesLeft();
    return bytes_left;
  }
}

void IbvQP::DumpTrace() {
  if (qp_use_propose == QpSendData)
  {  
    std::string filename = "/etc/CLEM/QpLogs/" + GetDebugName() + "_normal.log";
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
      std::cerr << "无法打开文件 " << filename << std::endl;
      return;
    }
    // 将向量中的每个元素逐行写入文件
    for (const auto& line : lines) {
      outFile << line << std::endl;
    }

    // 关闭文件
    outFile.close();

    std::cout << filename << " 文件已成功写入" << std::endl;
  }
}

void IbvQP::PrintTrafficBytes()
{
  std::string src_ip = std::to_string(m_src_ip.Get());
  std::string dst_ip = std::to_string(m_dst_ip.Get());
  std::string src_port = std::to_string(m_src_port);
  std::string dst_port = std::to_string(m_dst_port);

  std::string fivetuple = src_ip + "_" + dst_ip + "_" + src_port + "_" + dst_port + "_UDP";
  std::string qp_use_propose_str = QpUseTypeToString(qp_use_propose);
  uint64_t total_tx_bytes_1 = 0;
  uint64_t total_tx_bytes_2 = 0;
  switch (qp_use_propose)
  {
  case QpPostFifo:
    total_tx_bytes_1 = total_post_write_bytes;
    total_tx_bytes_2 = total_comp_write_bytes;
    break;
  case QpSendData:
    total_tx_bytes_1 = total_post_write_imm_bytes;
    total_tx_bytes_2 = total_comp_write_imm_bytes;
    break;
  default:
    break;
  }
  // NS_LOG_LOGIC("Connection: " << GetDebugName() << " <==> " << m_remote_qp->GetDebugName());
  printf("%s (with remote_qp=%s): five_tuple %s qp_use_propose %s total_tx_bytes: %llu %llu\n",
          GetDebugName().c_str(),m_remote_qp->GetDebugName().c_str(),fivetuple.c_str(),qp_use_propose_str.c_str(),total_tx_bytes_1,total_tx_bytes_2);
}

void IbvQP::StartApplication(void)
{

}
void IbvQP::StopApplication(void)
{
}
void IbvQP::SetQPType(enum ibv_qp_type qp_type)
{
  m_qp_type = qp_type;
}
}