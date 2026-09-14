#ifndef IBV_INTERFACE_H
#define IBV_INTERFACE_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ibv-cq.h"
#include "ns3/ibv-qp.h"
#include "ns3/qbb-net-device.h"
#include "ns3/ibvcore.h"
#include "ns3/ipv4-address.h"

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

#define SRC_PORT_START_INDEX 10000
namespace ns3 {
enum IbvCommandType {
  IBV_CREATE_QP,
  IBV_CREATE_CQ,
  IBV_POST_SEND,
  IBV_POST_RECV,
  IBV_POLL_CQ,
  IBV_MODIFY_QP,
  IBV_DESTROY_QP,
  IBV_DESTROY_CQ,
  IBV_QUERY_QP,
  IBV_QUERY_TIME
};

struct IbvCommand{
  uint32_t cmd_id;
  enum IbvCommandType cmd_type;
  union {
    struct {
      int device_id;
      struct shm_ibv_qp_init_attr qp_init_attr;
    } create_qp_input_args;
    struct {
      int device_id;
      int cqe;
      int comp_vector;
    } create_cq_input_args;  
    struct {
      char shm_qp_name[40];
      struct ibv_qp_attr attr;
      int attr_mask;
    } modify_qp_input_args;
    struct {
      char shm_qp_name[40];
      struct shm_ibv_send_wr wr;
    } post_send_input_args;
    struct {
      char shm_qp_name[40];
      struct shm_ibv_recv_wr wr;
    } post_recv_input_args;
    struct {
      char shm_qp_name[40];
      int attr_mask;
    } query_qp_input_args;
    struct {
      char shm_qp_name[40];
    } destroy_qp_input_args;
    struct {
      char shm_cq_name[40];
    } destroy_cq_input_args;
    struct {
      int64_t nosense;
    } query_time_input_args;
    
  } cmd_args;
};

struct IbvCommandResponse{
  uint32_t cmd_id;
  enum IbvCommandType cmd_type;
  union {
    struct {
      char qp_name[40];
      int qp_num;
    } create_qp_output_args;
    struct {
      char cq_name[40];
    } create_cq_output_args;
    struct {
      int retval;
    } modify_qp_output_args;
    struct {
      int retval;
    } post_send_output_args;
    struct {
      int retval;
    } post_recv_output_args;
    struct {
      int retval;
      struct ibv_qp_attr attr;
      struct shm_ibv_qp_init_attr init_attr;
    } query_qp_output_args;
    struct {
      int retval;
    } destroy_qp_output_args;
    struct {
      int retval;
    } destroy_cq_output_args;
    struct {
      int64_t time_ns;
    } query_time_output_args;
  } cmd_reponse;
};

struct PostFifoCtrlMsg {
  pid_t     pid;
  uintptr_t mem_addr;
  uint32_t  mem_length;
  struct ncclIbSendFifo msg_data;
};

bool IsNVLinkDev(Ptr<QbbNetDevice> dev);
class IbvCQ;
class IbvQP;
class IbvInterface : public Application
{
public:
  // Artificial delay before passing the Response Message to the upper-layer application via IPC; default is 0.
  int ipc_sleep_time;
  // static std::map<int,std::string> m_node2hca_map;
  static TypeId GetTypeId (void);
  static IbvCommandResponse UpdateCurTime(IbvCommand &cmd);
  IbvInterface();
  virtual ~IbvInterface();

  void SetRoutingTableIpv4Address(Ipv4Address addr);
  void SetRoutingTableMaskAddress(Ipv4Mask    mask);
  
  pid_t GetPidOfNode(int node_id);
  void ExecPostFifo(struct ncclIbSendFifo sendFifo, int remote_node_id, uintptr_t remote_addr);

  void EnableGpuP2p();
  void DisableGpuP2p();
  void SetGpusPerServer(int num_gpus_per_server);

  void UploadDeviceInfo();
  void UploadDeviceAttr();
  // 因为ns3固定了一个device只有一个port
  // 所以device、device_attr、port_attr是一一对应的
  void UploadDevicePortAttr();
  void UploadDeviceGidAttr();
  ibv_gid *GetDeviceGidAttr(int devIdx);
  std::vector<Ptr<IbvQP>> GetActiveIbvQps();
  Ptr<IbvQP> GetQP(uint32_t qp_num);
  Ptr<IbvQP> GetQP(int devIdx, std::string qp_name);
  Ptr<IbvCQ> GetCQ(int devIdx, std::string cq_name);

  void PollCommand();
  
  IbvCommandResponse ProcessCommand(IbvCommand& cmd);
  IbvCommandResponse CreateCQ(IbvCommand& cmd);
  IbvCommandResponse CreateQP(IbvCommand& cmd);
  IbvCommandResponse ModifyQP(IbvCommand& cmd);
  IbvCommandResponse DestroyQP(IbvCommand& cmd);
  IbvCommandResponse DestroyCQ(IbvCommand& cmd);
  IbvCommandResponse PostSend(IbvCommand& cmd);
  IbvCommandResponse PostRecv(IbvCommand& cmd);
  IbvCommandResponse ProvideQPInfo(IbvCommand& cmd);
  
  uint64_t GetBytesLeft();
  std::string GetBytesLeftDetailedInfo();
  uint64_t GetPollCounter();
  int GetCmdQueueSize();
  
  int m_num_qbb_devices;
  std::vector<uint64_t> m_idx_guid_map;
  std::vector<uint16_t> m_idx_mtu_map;
  std::vector<uint64_t> m_idx_rate_map;
  std::vector<Ptr<QbbNetDevice>> m_idx_device_map;
  std::vector<int> m_ascending_cq_idxs;
  std::vector<int> m_ascending_qp_idxs;
  std::map<uint32_t,std::map<uint32_t,uint16_t>> m_ascending_src_port_nums;
  
  // node id (to docker id) to docker daemon process socket fd map
  static std::map<int,int> node2daemonsockfd_map;
  static std::map<int,pid_t> m_node2pid_map;
  void EnablePollPrint();

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  // shared memory交互的信息
  std::string m_segName;
  uint32_t m_segSize;
  boost::interprocess::managed_shared_memory * m_segment;
  // 因为网络设备数目不定，所以是指针数组
  // 简单粗暴直接对所有devices加锁
  ibv_device **m_ibv_devices;
  boost::interprocess::interprocess_mutex * m_ibv_devices_mutex;
  // 因为网络设备数目不定，所以是指针数组
  // 简单粗暴直接对所有devices加锁
  ibv_device_attr **m_ibv_devices_attr;
  boost::interprocess::interprocess_mutex * m_ibv_devices_attr_mutex;
  // 因为网络设备数目不定，所以是指针数组
  // 简单粗暴直接对所有devices加锁
  ibv_port_attr **m_ibv_devices_port_attr;
  boost::interprocess::interprocess_mutex * m_ibv_devices_port_attr_mutex;
  // 因为网络设备数目不定，所以是指针数组
  // 简单粗暴直接对所有devices加锁
  ibv_gid **m_ibv_devices_gid_attr;
  boost::interprocess::interprocess_mutex * m_ibv_devices_gid_attr_mutex;
  int *m_ibv_num_devices;
  boost::interprocess::interprocess_mutex * m_ibv_num_devices_mutex;
  // 轮询上层应用的命令 例如 post_send post_recv poll_cq 等等
  // 然后响应对应的命令 
  typedef boost::interprocess::allocator<IbvCommand, boost::interprocess::managed_shared_memory::segment_manager> IbvCommandAllocator;
  typedef boost::interprocess::allocator<IbvCommandResponse, boost::interprocess::managed_shared_memory::segment_manager> IbvCommandResponseAllocator;
  typedef boost::interprocess::vector<IbvCommand, IbvCommandAllocator> CommandQueue;
  typedef boost::interprocess::vector<IbvCommandResponse, IbvCommandResponseAllocator> CommandResponseQueue;
  CommandQueue *m_cmd_queue;
  CommandResponseQueue *m_cmd_resp_queue;
  boost::interprocess::interprocess_mutex * m_cmd_queue_mutex;
  boost::interprocess::interprocess_mutex * m_cmd_resp_queue_mutex;

  typedef std::map<std::string, Ptr<IbvCQ>> CQContainerPerDevice;
  typedef std::map<std::string, Ptr<IbvQP>> QPContainerPerDevice;
  std::vector<CQContainerPerDevice> m_ibv_cqs;
  std::vector<QPContainerPerDevice> m_ibv_qps;
  std::map<std::string, Ptr<IbvQP>> m_total_qps;

  uint32_t m_ascending_qp_num;
  int m_num_gpus_per_server;
  bool m_enable_gpu_p2p;
  // 和轮询相关的变量  poll counter and poll time interval
  uint64_t m_accu_empty_counter;  // 轮询共享内存为空的累积次数
  uint64_t m_poll_counter;        // 总的轮询次数 用于调试
  uint64_t m_poll_empty_cnt;      // 总的轮训结果为空的次数 用于调试
  bool     m_already_print;       // 是否已经打印轮询统计结果
  uint64_t m_poll_interval;       // 轮询时间间隔
  uint64_t m_send_latency;
  bool m_poll_print;
  // gid address returns routing table address
  Ipv4Address m_addr;
  Ipv4Mask m_mask;
};
}

#endif // IBV_INTERFACE_H