#ifndef IBV_CQ_H
#define IBV_CQ_H

#include "ns3/object.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ibvcore.h"
#include "ns3/ipv4-address.h"
#include "ns3/qbb-net-device.h"
#include "ns3/node.h"
#include <ns3/rdma.h>
#include <vector>
#include <map>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace ns3 {
class IbvCQ : public Object {
public:
  static TypeId GetTypeId (void);
  IbvCQ();
  typedef boost::interprocess::allocator<shm_ibv_wc, boost::interprocess::managed_shared_memory::segment_manager> CQEAllocator;
  typedef boost::interprocess::vector<shm_ibv_wc, CQEAllocator> CQEQueue;
  std::string GetName();
  uint32_t GetCqeCount();

  void SetName(std::string name);
  Ptr<QbbNetDevice> GetDevice();
  void SetDevice(Ptr<QbbNetDevice> qbbDevice);
  Ptr<Node> GetNode();
  void SetNode(Ptr<Node> node);
  
  void SetShmSegment(boost::interprocess::managed_shared_memory * segment);
  void Initialize();
  void GenerateSendCQE(shm_ibv_send_wr wr, uint32_t local_qpn, uint32_t remote_qpn);
  void GenerateRecvCQE(shm_ibv_recv_wr wr, uint32_t local_qpn, uint32_t remote_qpn, uint32_t imm_data, bool is_imm_data_valid);
  
private:
  Ptr<Node> m_node;
  bool m_is_valid;
  Ptr<QbbNetDevice> m_device;
  std::string m_name;
  boost::interprocess::managed_shared_memory * m_segment;
  CQEQueue * m_cq_container;
  boost::interprocess::interprocess_mutex *m_cq_container_mutex;
  uint32_t m_cqe_count;
};
}
#endif