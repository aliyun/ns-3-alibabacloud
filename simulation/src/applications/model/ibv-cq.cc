#include "ibv-cq.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/string.h"
#include "ns3/qbb-net-device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ns3/assert.h"
#include <chrono>

namespace ns3{

NS_LOG_COMPONENT_DEFINE("IbvCQ");
NS_OBJECT_ENSURE_REGISTERED(IbvCQ);
TypeId IbvCQ::GetTypeId(void)
{
  LogSetTimePrinter(CustomTimePrinter);
  static TypeId tid = TypeId("ns3::IbvCQ")
    .SetParent<Object>()
    .AddConstructor<IbvCQ>();
  return tid;
}

IbvCQ::IbvCQ() : m_is_valid(true), m_cq_container(NULL), m_cqe_count(0)
{
}

std::string IbvCQ::GetName()
{
  return m_name;
}

uint32_t IbvCQ::GetCqeCount()
{
  return m_cqe_count;
}

void IbvCQ::SetName(std::string name)
{
  m_name = name;
}

Ptr<QbbNetDevice> IbvCQ::GetDevice()
{
  return m_device;
}

void IbvCQ::SetDevice(Ptr<QbbNetDevice> qbbDevice)
{
  m_device = qbbDevice;
}

Ptr<Node> IbvCQ::GetNode()
{
  return m_node;
}

void IbvCQ::SetNode(Ptr<Node> node)
{
  m_node = node;
}

void IbvCQ::SetShmSegment(boost::interprocess::managed_shared_memory *segment)
{
  m_segment = segment;
}
void IbvCQ::Initialize()
{
  const CQEAllocator alloc_cqe(m_segment->get_segment_manager());
  std::string cq_container_name = m_name + "_container";
  std::string cq_container_mutex_name = m_name + "_container_mutex";
  m_cq_container = m_segment->construct<CQEQueue>(cq_container_name.c_str())(alloc_cqe);
  m_cq_container_mutex = m_segment->construct<boost::interprocess::interprocess_mutex>
                        (cq_container_mutex_name.c_str())();
  if(!m_cq_container) {
    NS_FATAL_ERROR("Failed to create CQEQueue");
  }
}
void IbvCQ::GenerateSendCQE(shm_ibv_send_wr wr,uint32_t local_qpn,uint32_t remote_qpn)
{
  NS_LOG_FUNCTION(this);
  m_cqe_count ++;
  struct shm_ibv_wc wc;
  memset(&wc, 0, sizeof(wc));
  wc.wr_id = wr.wr_id;
  wc.status = IBV_WC_SUCCESS;
  wc.qp_num = local_qpn;
  wc.src_qp = remote_qpn;
  wc.byte_len = wr.total_length;
  wc.imm_data = wr.imm_data; 
  switch (wr.opcode) {
    case IBV_WR_RDMA_WRITE:
      wc.opcode = IBV_WC_RDMA_WRITE;
      break;
    case IBV_WR_RDMA_WRITE_WITH_IMM:
      wc.opcode = IBV_WC_RDMA_WRITE;
      break;
    case IBV_WR_RDMA_READ:
      wc.opcode = IBV_WC_RDMA_READ;
      break;
    default:
      NS_FATAL_ERROR("Unknown/Unsupported opcode");
  }
  ENQUEUE(m_cq_container, wc);
  NS_LOG_LOGIC("IbvCQ(" << m_node->GetId() << "," << local_qpn << ") GenerateSendCQE: wr_id=" << wr.wr_id << " opcode=" << wr.opcode << " byte_len=" << wc.byte_len);
}
void IbvCQ::GenerateRecvCQE(shm_ibv_recv_wr wr, uint32_t local_qpn, uint32_t remote_qpn, uint32_t imm_data, bool is_imm_data_valid)
{
  NS_LOG_FUNCTION(this);
  if(!is_imm_data_valid) {
    NS_LOG_ERROR("Only support IBV_WR_RDMA_WRITE_WITH_IMM, not support IBV_WR_SEND");
    NS_FATAL_ERROR("Only support IBV_WR_RDMA_WRITE_WITH_IMM, not support IBV_WR_SEND");
  }
  // struct shm_ibv_wc {
  //   uint64_t		wr_id;
  //   enum ibv_wc_status	status;
  //   enum ibv_wc_opcode	opcode;
  //   uint32_t		vendor_err;
  //   uint32_t		byte_len;
  //   uint32_t		imm_data;	/* in network byte order */
  //   uint32_t		qp_num;
  //   uint32_t		src_qp;
  //   int			wc_flags;
  //   uint16_t		pkey_index;
  //   uint16_t		slid;
  //   uint8_t			sl;
  //   uint8_t			dlid_path_bits;	
  // };
  struct shm_ibv_wc wc;
  memset(&wc, 0, sizeof(wc));
  wc.wr_id = wr.wr_id;
  wc.status = IBV_WC_SUCCESS;
  wc.opcode = IBV_WC_RECV_RDMA_WITH_IMM;
  wc.imm_data = imm_data;
  wc.byte_len = 4;
  wc.qp_num = local_qpn;
  wc.src_qp = remote_qpn;
  wc.wc_flags = IBV_WC_WITH_IMM;
  wc.qp_num = local_qpn;
  ENQUEUE(m_cq_container, wc);
  NS_LOG_LOGIC("IbvCQ(" << m_node->GetId() << "," << local_qpn << ") GenerateRecvCQE: wr_id=" << wr.wr_id);
}
}