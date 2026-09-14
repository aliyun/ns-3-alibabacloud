#ifndef IBV_QP_H
#define IBV_QP_H


#include "ns3/object.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/node.h"
#include "ns3/ibvcore.h"
#include "ns3/ipv4-address.h"
#include "ns3/object-factory.h"
#include <ns3/rdma.h>
#include "ns3/ibv-cq.h"
#include "ns3/rdma-queue-pair.h"
#include "ns3/qbb-net-device.h"
#include "ns3/ibv-interface.h"
#include "ns3/application.h"
// #include "ibv-cq.h"
#include <vector>
#include <queue>
#include <map>
#include <fstream>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#define POST_FIFO_IMM 15555
namespace ns3 {

class IbvCQ;
class IbvInterface;
class RdmaQueuePair;
class QbbNetDevice;
class Node;
class IbvQP : public Application {
public:
  IbvQP();
  // virtual ~IbvQP();
  static TypeId GetTypeId (void);
  

  void Nop(std::string op_name);
  uint32_t CalculateWindow(uint32_t bdp);

  void PrintQpState();
  
  void EnqueueSendRequest(shm_ibv_send_wr send_wr);
  void EnqueueRecvRequest(shm_ibv_recv_wr recv_wr);
  void ConsumeRecvWrAndGenerateRecvCQE(shm_ibv_send_wr send_wr); // 参数类型就是send_wr 因为我们需要根据send_wr的类型来判断如何消耗recv_wr
  void WrSent(Ptr<RdmaQueuePair> qp, shm_ibv_send_wr wr);
  void WrCompleted(Ptr<RdmaQueuePair> qp, shm_ibv_send_wr wr);

  void SetShmSegment(boost::interprocess::managed_shared_memory * segment);
  void SetSendCQ(Ptr<IbvCQ> cq);
  void SetRecvCQ(Ptr<IbvCQ> cq);
  void SetName(std::string name);
  void SetDevice(Ptr<QbbNetDevice> qbbDevice);
  void SetQPCap(struct ibv_qp_cap	cap);
  void SetQPType(enum ibv_qp_type	qp_type);
	void SetSqSigAll(int sq_sig_all);
  void SetQPNum(uint32_t qp_num);
  void SetDstQPNum(uint32_t dst_qp_num);
  void SetQPState(enum ibv_qp_state state);
  void SetAddressAttr(ibv_ah_attr ah_attr);
  void SetPg(uint16_t pg);
  void SetSrcPort(uint16_t port);
  void SetDstPort(uint16_t port);
  void SetPairBdp(uint32_t bdp);
  void SetPairRtt(uint64_t rtt);
  void SetGpusPerServer(int num_gpus_per_server);
  void EnableVarWin();
  void DisableVarWin();
  void EnableP2P();
  void DisableP2P();
  void SetNicIdx(uint32_t nic_idx);
  void SetIbvInterface(Ptr<IbvInterface> ibvInterface);
  
  
  Ptr<IbvCQ> GetSendCQ();
  Ptr<IbvCQ> GetRecvCQ();
  std::string GetName();
  std::string GetDebugName();
  Ptr<QbbNetDevice> GetDevice();
  struct ibv_qp_cap GetQPCap();
  enum ibv_qp_type GetQPType();
  int GetSqSigAll();
  uint32_t GetQPNum();
  uint32_t GetDstQPNum();
  enum ibv_qp_state GetQPState();
  ibv_ah_attr GetAddressAttr();
  uint16_t GetPg();
  uint16_t GetSrcPort();
  uint16_t GetDstPort();
  uint32_t GetSrcIP();
  uint32_t GetDstIP();
  uint32_t GetPairBdp();
  uint64_t GetPairRtt();
  uint32_t GetNicIndex();
  Ptr<RdmaQueuePair> GetRdmaQueuePair();
  Ptr<IbvInterface> GetIbvInterface();
  Ptr<IbvQP> GetRemoteQP();
  void SetSrcIP(Ipv4Address ip);
  void SetDstIP(Ipv4Address ip);
  void QPCommInit();
  // 把所有调试的信息写入到文件中
  void DumpTrace();
  void PrintTrafficBytes();
  
  uint64_t GetBytesLeft();

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  // ObjectFactory m_factory;
  bool m_is_valid;
  // 当本端的RdmaQueuePair被添加至网卡之后 m_is_rts = true
  bool m_is_rts;
  Ptr<QbbNetDevice> m_device;
  Ptr<Node> m_remote_node;
  Ptr<IbvQP> m_remote_qp;
  uint32_t m_nic_idx;
  std::string m_name;
  uint16_t m_pg;
  struct ibv_qp_cap	m_cap;
	enum ibv_qp_type	m_qp_type;
	int			          m_sq_sig_all;
  uint32_t		m_qp_num;
  uint32_t    m_dst_qp_num;
	enum ibv_qp_state       m_qp_state;
  ibv_ah_attr m_ah_attr;
  Ipv4Address m_src_ip;
  Ipv4Address m_dst_ip;
  uint16_t m_src_port;
  uint16_t m_dst_port;
  uint32_t m_pair_bdp;
  uint64_t m_pair_rtt;
  bool m_enable_var_win;
  bool m_enable_gpu_p2p;
  int m_num_gpus_per_server;
  
  boost::interprocess::managed_shared_memory * m_segment;
  Ptr<IbvCQ> m_send_cq;
  Ptr<IbvCQ> m_recv_cq;
  Ptr<IbvInterface> m_ibv_interface;
  Ptr<RdmaQueuePair> m_qp;
  std::queue<shm_ibv_recv_wr> m_waiting_recv_wrs;
  // For Debug
  std::vector<std::string> lines;
  uint64_t total_post_write_bytes;
  uint64_t total_post_write_imm_bytes;
  uint64_t total_comp_write_bytes;
  uint64_t total_comp_write_imm_bytes;
  bool is_type_confirmed;
  enum QpUseType {
    QpPostFifo,
    QpSendData,
    QpRecvData,
    QpFlushRead,
    QpUnknown
  };
  static std::string QpUseTypeToString(enum QpUseType qp_use_propose) {
    std::string qp_use_propose_str;
    switch (qp_use_propose)
    {
    case QpPostFifo:  qp_use_propose_str = "QpPostFifo";  break;
    case QpSendData:  qp_use_propose_str = "QpSendData";  break;
    case QpRecvData:  qp_use_propose_str = "QpRecvData";  break;
    case QpFlushRead: qp_use_propose_str = "QpFlushRead"; break;
    case QpUnknown:   qp_use_propose_str = "QpUnknown";   break;
    default:                                              break;
    }
    return qp_use_propose_str;
  }
  enum QpUseType qp_use_propose;
};
}
#endif