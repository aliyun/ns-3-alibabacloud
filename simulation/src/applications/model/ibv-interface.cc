#include "ibv-interface.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/string.h"
#include "ns3/qbb-net-device.h"
#include "ns3/data-rate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include "ns3/assert.h"

#define MAX_POLL_COUNT 128

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("IbvInterface");
NS_OBJECT_ENSURE_REGISTERED(IbvInterface);

std::map<int, int>  ns3::IbvInterface::node2daemonsockfd_map;
std::map<int,pid_t> ns3::IbvInterface::m_node2pid_map;

TypeId IbvInterface::GetTypeId(void)
{
  // LogSetTimePrinter(CustomTimePrinter);
  static TypeId tid = TypeId("ns3::IbvInterface")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<IbvInterface>()
    .AddAttribute("SegmentName","Shared memory segment name",
                  StringValue("ibv_shared_memory"),
                  MakeStringAccessor(&IbvInterface::m_segName),
                  MakeStringChecker())
    .AddAttribute("SegmentSize","Shared memory segment size",
                  UintegerValue(64*1024*1024),
                  MakeUintegerAccessor(&IbvInterface::m_segSize),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("PollTimeInterval","Time interval for polling command queue (NanoSeconds)",
                  UintegerValue(500),
                  MakeUintegerAccessor(&IbvInterface::m_poll_interval),
                  MakeUintegerChecker<uint64_t>())
    .AddAttribute("SendLatency","Send latency of ibv_post_send",
                  UintegerValue(4000),
                  MakeUintegerAccessor(&IbvInterface::m_send_latency),
                  MakeUintegerChecker<uint64_t>());
  return tid;
}

IbvCommandResponse IbvInterface::UpdateCurTime(IbvCommand &cmd)
{
  IbvCommandResponse resp;
  resp.cmd_type = IBV_QUERY_TIME;
  resp.cmd_id = cmd.cmd_id;
  resp.cmd_reponse.query_time_output_args.time_ns = Simulator::Now().GetNanoSeconds();
  return resp;
}

pid_t IbvInterface::GetPidOfNode(int node_id) {
  if(m_node2pid_map.find(node_id) == m_node2pid_map.end()) {
    std::string remote_seg_name = SHM_NAME_PREFIX + std::to_string(node_id);
    std::string pid_name = "pid_of_node" + std::to_string(node_id);
    boost::interprocess::managed_shared_memory remote_segment(boost::interprocess::open_only, remote_seg_name.c_str());
    int *remote_pid_ptr = remote_segment.find<int>(pid_name.c_str()).first;
    if(!remote_pid_ptr) {
      printf("post_send_data_transport: failed to find remote pid of node %d", node_id);
      return -1;
    }
    pid_t pid = *remote_pid_ptr;
    m_node2pid_map[node_id] = pid;
  }
  return m_node2pid_map[node_id];
}

ssize_t write_remote_memory(pid_t pid, const void *buffer, void *addr, size_t size) {
  char path[50];
  sprintf(path, "/proc/%d/mem", pid);
  int fd = open(path, O_WRONLY);
  if (fd == -1) {
    char info[100];
    sprintf(info, "fail to open /proc/%d/mem", pid);
    perror(info);
    return -1;
  }

  ssize_t bytes_written = pwrite(fd, buffer, size, (uintptr_t)addr);
  if (bytes_written == -1) {
    perror("fail to write to remote memory");
  } else {
    // printf("daemon: success to write %u bytes\n", bytes_written);
  }

  close(fd);
  return bytes_written;
}

void IbvInterface::ExecPostFifo(ncclIbSendFifo sendFifo, int remote_node_id, uintptr_t remote_addr) {
  // int send_sockfd = node2daemonsockfd_map[remote_node_id];
  // NS_LOG_LOGIC("11111");
  pid_t remote_pid = GetPidOfNode(remote_node_id);
  if(remote_pid==-1) {
    NS_FATAL_ERROR("failed to find remote pid of node " << remote_node_id);
  }
  // NS_LOG_LOGIC("22222");
  PostFifoCtrlMsg msg;
  ssize_t response;
  msg.pid = remote_pid;
  msg.mem_addr = remote_addr;
  msg.mem_length = sizeof(sendFifo);
  msg.msg_data = sendFifo;
  // NS_LOG_LOGIC("33333");
  struct ncclIbSendFifo* buffer = &msg.msg_data;
  // printf("ncclIbSendFifo.wr_id =  %d\n", buffer->wr_id);
  ssize_t res = write_remote_memory(msg.pid, (void*)buffer, (void*)msg.mem_addr, msg.mem_length);
  // NS_LOG_LOGIC("write_remote_memory " << res << " bytes");
  return;
}
IbvInterface::IbvInterface() : m_segName("ibv_shared_memory"), m_segSize(64*1024*1024), m_poll_counter(0), m_accu_empty_counter(0), m_poll_print(false), m_poll_empty_cnt(0), m_already_print(0)
{
  
}
IbvInterface::~IbvInterface()
{
}

void IbvInterface::SetRoutingTableIpv4Address(Ipv4Address addr)
{
  m_addr = addr;
}

void IbvInterface::SetRoutingTableMaskAddress(Ipv4Mask mask)
{
  m_mask = mask;
}

void IbvInterface::EnableGpuP2p()
{
  m_enable_gpu_p2p = true;
}
void IbvInterface::DisableGpuP2p()
{
  m_enable_gpu_p2p = false;
}

void IbvInterface::SetGpusPerServer(int num_gpus_per_server)
{
  m_num_gpus_per_server = num_gpus_per_server;
}

void IbvInterface::UploadDeviceInfo() {
  LOCK_MUTEX(m_ibv_num_devices);
  LOCK_MUTEX(m_ibv_devices);
  int num_total_devices = m_node->GetNDevices();
  int num_qbb_devices = 0;
  // for(int i=0;i<num_total_devices;i++) {
  //   Ptr<NetDevice> device = m_node->GetDevice(i);
  //   if (device->IsQbb()) {
  //     Ptr<QbbNetDevice> qbbDevice = DynamicCast<QbbNetDevice>(device);      
  //     if (qbbDevice && !IsNVLinkDev(qbbDevice)) {
  //       uint64_t high_part = static_cast<uint64_t>(m_node->GetId()) << 32;
  //       uint64_t low_part = static_cast<uint64_t>(qbbDevice->GetIfIndex());
  //       uint64_t identifier = high_part | low_part;
  //       m_idx_guid_map.push_back(identifier);
  //       m_idx_mtu_map.push_back(qbbDevice->GetMtu());
  //       m_idx_rate_map.push_back(qbbDevice->GetDataRate().GetBitRate());
  //       m_idx_device_map.push_back(qbbDevice);
  //       if ((m_idx_mtu_map.back() != (uint16_t)256) &&
  //           (m_idx_mtu_map.back() != (uint16_t)512) &&
  //           (m_idx_mtu_map.back() != (uint16_t)1024) &&
  //           (m_idx_mtu_map.back() != (uint16_t)2048) &&
  //           (m_idx_mtu_map.back() != (uint16_t)4096)) {
  //         qbbDevice->SetMtu(1024);
  //         m_idx_mtu_map.back() = 1024;
  //       }
  //       NS_LOG_LOGIC("IbvInterface: mtu = " << m_idx_mtu_map.back());
  //       if ((m_idx_rate_map.back() != (uint64_t)1e11) &&
  //           (m_idx_rate_map.back() != (uint64_t)2e11) && 
  //           (m_idx_rate_map.back() != (uint64_t)4e11) && 
  //           (m_idx_rate_map.back() != (uint64_t)8e11)) {
  //         NS_LOG_LOGIC("IbvInterface: rate = " << m_idx_rate_map.back());
  //         qbbDevice->SetDataRate(DataRate((uint64_t)1e11));
  //         m_idx_rate_map.back() = (uint64_t)1e11;
  //       }
  //       NS_LOG_LOGIC("IbvInterface: rate = " << m_idx_rate_map.back());
  //       num_qbb_devices++;

  //     }
  //   }
  // }
  m_ibv_num_devices = m_segment->construct<int>("num_devices")();
  num_qbb_devices = 1;
  *m_ibv_num_devices = num_qbb_devices;
  NS_LOG_LOGIC("IbvInterface: num_devices = " << num_qbb_devices);
  m_num_qbb_devices = num_qbb_devices;
  // TODO: free memory
  m_ibv_devices  = (struct ibv_device **)malloc(num_qbb_devices*sizeof(struct ibv_device*));
  for(int i=0;i<num_qbb_devices;i++) {
    std::string device_name = "qbb_net_device_" + std::to_string(i);
    m_ibv_devices[i] = m_segment->construct<ibv_device>(device_name.c_str())();
    m_ibv_devices[i]->node_type = IBV_NODE_CA;
    m_ibv_devices[i]->transport_type = IBV_TRANSPORT_IB;
    sprintf(m_ibv_devices[i]->name, "ns3_qbbdev_%d", i);
  }
  return;
}
void IbvInterface::UploadDeviceAttr()
{
  LOCK_MUTEX(m_ibv_devices_attr);
  // TODO: free memory
  m_ibv_devices_attr = (struct ibv_device_attr **)malloc(m_num_qbb_devices*sizeof(struct ibv_device_attr*));
  for(int i=0;i<m_num_qbb_devices;i++) {
    std::string device_attr_name = "qbb_net_device_attr_" + std::to_string(i);
    m_ibv_devices_attr[i] = m_segment->construct<ibv_device_attr>(device_attr_name.c_str())();
    m_ibv_devices_attr[i]->node_guid = m_node->GetId();
    m_ibv_devices_attr[i]->sys_image_guid = m_node->GetId();
    m_ibv_devices_attr[i]->max_qp = 131072;
    m_ibv_devices_attr[i]->local_ca_ack_delay = 16;
    m_ibv_devices_attr[i]->phys_port_cnt = 1;
  }
  return;
}

void IbvInterface::UploadDevicePortAttr()
{
  // printf("UploadDevicePortAttr 0\n");
  LOCK_MUTEX(m_ibv_devices_port_attr);
  // TODO: free memory
  m_ibv_devices_port_attr = (struct ibv_port_attr **)malloc(m_num_qbb_devices*sizeof(struct ibv_port_attr*));
  // printf("m_num_qbb_devices: %d\n",m_num_qbb_devices);
  for(int i=0;i<m_num_qbb_devices;i++) {
    std::string device_port_attr_name = "device_" + std::to_string(i) + "_port_attr";
    m_ibv_devices_port_attr[i] = m_segment->construct<ibv_port_attr>(device_port_attr_name.c_str())();
    m_ibv_devices_port_attr[i]->state = IBV_PORT_ACTIVE;
    m_ibv_devices_port_attr[i]->max_mtu = IBV_MTU_1024;
    m_ibv_devices_port_attr[i]->active_mtu = IBV_MTU_1024;
    m_ibv_devices_port_attr[i]->gid_tbl_len = 255;
    m_ibv_devices_port_attr[i]->port_cap_flags = 0x4010000;
    m_ibv_devices_port_attr[i]->max_msg_sz = 1073741824;
    m_ibv_devices_port_attr[i]->bad_pkey_cntr = 0;
    m_ibv_devices_port_attr[i]->qkey_viol_cntr = 0;
    m_ibv_devices_port_attr[i]->pkey_tbl_len = 1;
    m_ibv_devices_port_attr[i]->lid = 0;
    m_ibv_devices_port_attr[i]->sm_lid = 0;
    m_ibv_devices_port_attr[i]->lmc = 0;
    m_ibv_devices_port_attr[i]->max_vl_num = 0;
    m_ibv_devices_port_attr[i]->sm_sl = 0;
    m_ibv_devices_port_attr[i]->subnet_timeout = 0;
    m_ibv_devices_port_attr[i]->init_type_reply = 0;
    m_ibv_devices_port_attr[i]->active_width = 2;
    m_ibv_devices_port_attr[i]->active_speed = 32;
    // switch (m_idx_rate_map[i])
    // {
    // case (uint64_t)1e11:
    //   m_ibv_devices_port_attr[i]->active_width = 2;
    //   m_ibv_devices_port_attr[i]->active_speed = 32;
    //   break;
    // case (uint64_t)2e11:
    //   m_ibv_devices_port_attr[i]->active_width = 2;
    //   m_ibv_devices_port_attr[i]->active_speed = 64;
    //   break;
    // case (uint64_t)4e11:
    //   m_ibv_devices_port_attr[i]->active_width = 2;
    //   m_ibv_devices_port_attr[i]->active_speed = 128;
    //   break;
    // case (uint64_t)8e11:
    //   m_ibv_devices_port_attr[i]->active_width = 4;
    //   m_ibv_devices_port_attr[i]->active_speed = 128;
    //   break;
    // default:
    //   m_ibv_devices_port_attr[i]->active_width = 2;
    //   m_ibv_devices_port_attr[i]->active_speed = 128;
    //   break;
    // }
    m_ibv_devices_port_attr[i]->phys_state = 5;
    m_ibv_devices_port_attr[i]->link_layer = IBV_LINK_LAYER_ETHERNET;
    m_ibv_devices_port_attr[i]->reserved = 0;
  }
  return;
}

void IbvInterface::UploadDeviceGidAttr()
{
  LOCK_MUTEX(m_ibv_devices_gid_attr);
  m_ibv_devices_gid_attr = (ibv_gid **)malloc(m_num_qbb_devices*sizeof(ibv_gid*));

  for(int i=0;i<m_num_qbb_devices;i++) {
    std::string device_gid_attr_name = "device_" + std::to_string(i) + "_gid";
    m_ibv_devices_gid_attr[i] = m_segment->construct<ibv_gid>(device_gid_attr_name.c_str())();
    memset(m_ibv_devices_gid_attr[i]->raw, 0, 16);
    Ipv4Address local_addr = m_addr;  // 获取IPv4地址
    uint64_t high_part = static_cast<uint64_t>(m_node->GetId()) << 32;
    uint64_t low_part = static_cast<uint64_t>(local_addr.Get());
    uint64_t identifier = high_part | low_part;

    Ipv4Mask local_addr_mask = m_mask; // 获取子网掩码
    uint64_t net_mask = static_cast<uint64_t>(local_addr_mask.Get());
    m_ibv_devices_gid_attr[i]->global.subnet_prefix = net_mask;
    m_ibv_devices_gid_attr[i]->global.interface_id = identifier;

  }
  return;
}

ibv_gid *IbvInterface::GetDeviceGidAttr(int devIdx)
{
  if(!m_ibv_devices_gid_attr) {
    return NULL;
  } else {
    return m_ibv_devices_gid_attr[devIdx];
  }
}

std::vector<Ptr<IbvQP>> IbvInterface::GetActiveIbvQps() {
  std::vector<Ptr<IbvQP>> m_active_qps;
  for(auto named_qp: m_total_qps) {
    m_active_qps.push_back(named_qp.second);
  }
  return m_active_qps;
}

Ptr<IbvQP> IbvInterface::GetQP(uint32_t qp_num)
{
  for(auto named_qp: m_total_qps) {
    if(named_qp.second->GetQPNum() == qp_num) {
      return named_qp.second;
    }
  }
  return nullptr;
}

Ptr<IbvCQ> IbvInterface::GetCQ(int devIdx, std::string cq_name)
{
  return m_ibv_cqs[devIdx][cq_name];
}

Ptr<IbvQP> IbvInterface::GetQP(int devIdx, std::string qp_name)
{
  return m_ibv_qps[devIdx][qp_name];
}

int IbvInterface::GetCmdQueueSize() {
  return m_cmd_queue->size();
}

void IbvInterface::PollCommand() {
  m_poll_counter ++;
  size_t cmd_queue_size = m_cmd_queue->size();
  if(cmd_queue_size == 0) {
    m_poll_empty_cnt += 1;
    // 轮空 直接下一轮
    m_accu_empty_counter += 1;
    Simulator::Schedule(NanoSeconds(m_poll_interval), &IbvInterface::PollCommand, this);
    if(m_poll_print && m_poll_counter % 10 == 0) {
      NS_LOG_LOGIC("Node " << m_node->GetId() << " poll 0 cmds");
    }
    // NS_LOG_LOGIC("Node " << m_node->GetId() << " 轮空 直接下一轮");
  } else {
    // NS_LOG_LOGIC("Node " << m_node->GetId() << " After " << m_accu_empty_counter << " empty rounds");
    m_accu_empty_counter = 0;
    IbvCommand *cmds = (IbvCommand *)malloc(cmd_queue_size*sizeof(IbvCommand));
    IbvCommandResponse *responses = (IbvCommandResponse *)malloc(cmd_queue_size*sizeof(IbvCommandResponse));
    LOCK_MUTEX(m_cmd_queue);
    // 获取并处理Command
    for(int i=0;i<cmd_queue_size && i<MAX_POLL_COUNT;i++) {
      cmds[i] = m_cmd_queue->front();
      m_cmd_queue->erase(m_cmd_queue->begin());
      responses[i] = ProcessCommand(cmds[i]);
      if(cmds[i].cmd_id % 500 == 0 || cmds[i].cmd_id < 500 || m_poll_print) {
        NS_LOG_LOGIC("Node " << m_node->GetId() << " cmd_id: " << cmds[i].cmd_id << " cmd_type: " << cmds[i].cmd_type);
      }
    }
    // 将Command的响应传给NCCL
    for(int i=0;i<cmd_queue_size && i<MAX_POLL_COUNT;i++) {
      IbvCommandResponse resp = responses[i];
      if(resp.cmd_type == IBV_POST_SEND || resp.cmd_type == IBV_POST_RECV) {
        continue;
      } else {
        usleep(ipc_sleep_time);
        ENQUEUE(m_cmd_resp_queue, resp);
      }
    }
    free(cmds);
    free(responses);
    // 注册下一次轮询的时刻
    Simulator::Schedule(NanoSeconds(m_poll_interval), &IbvInterface::PollCommand, this);
  }
}

IbvCommandResponse IbvInterface::ProcessCommand(IbvCommand &cmd)
{
  IbvCommandResponse resp;
  switch (cmd.cmd_type)
  {
  case IBV_CREATE_CQ:   resp = CreateCQ(cmd);       break;
  case IBV_CREATE_QP:   resp = CreateQP(cmd);       break;
  case IBV_QUERY_QP:    resp = ProvideQPInfo(cmd);  break;
  case IBV_MODIFY_QP:   resp = ModifyQP(cmd);       break;
  case IBV_POST_SEND:   resp = PostSend(cmd);       break;
  case IBV_POST_RECV:   resp = PostRecv(cmd);       break;
  case IBV_DESTROY_QP:  resp = DestroyQP(cmd);      break;
  case IBV_DESTROY_CQ:  resp = DestroyCQ(cmd);      break;
  case IBV_QUERY_TIME:  resp = UpdateCurTime(cmd);  break;
  default:
    NS_LOG_WARN("Unknown command type\n");
    NS_FATAL_ERROR("Unknown command type");
    break;
  }
  return resp;
}

IbvCommandResponse IbvInterface::CreateCQ(IbvCommand &cmd)
{
  int device_id = cmd.cmd_args.create_cq_input_args.device_id;
  NS_ASSERT(device_id >= 0 && device_id < m_num_qbb_devices);
  int cq_id = m_ascending_cq_idxs[device_id]++;
  std::string cq_name = "cq_" + std::to_string(device_id) + "_" + std::to_string(cq_id);
  NS_ASSERT(cq_name.length() < 40);
  IbvCQ* newCQ = m_segment->construct<IbvCQ>(cq_name.c_str())();
  newCQ->SetDevice(0);
  newCQ->SetName(cq_name);
  newCQ->SetShmSegment(m_segment);
  newCQ->SetNode(m_node);
  newCQ->Initialize();
  m_ibv_cqs[device_id][cq_name] = Ptr<IbvCQ>(newCQ);
  IbvCommandResponse resp;
  resp.cmd_id = cmd.cmd_id;
  resp.cmd_type = IBV_CREATE_CQ;
  strcpy(resp.cmd_reponse.create_cq_output_args.cq_name, cq_name.c_str());
  NS_LOG_LOGIC("Node " << m_node->GetId() << "create cq " << cq_name);
  NS_LOG_LOGIC("Node " << m_node->GetId() << "Response " << resp.cmd_reponse.create_cq_output_args.cq_name << " " << resp.cmd_id);
  return resp;
}

IbvCommandResponse IbvInterface::CreateQP(IbvCommand &cmd)
{
  int device_id = cmd.cmd_args.create_qp_input_args.device_id;
  NS_ASSERT(device_id >= 0 && device_id < m_num_qbb_devices);
  std::string send_cq_name = cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_send_cq_name;
  std::string recv_cq_name = cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_recv_cq_name;
  // printf("Command shm_send_cq_name: %s\n",cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_send_cq_name);
  // printf("Command shm_recv_cq_name: %s\n",cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_recv_cq_name);
  // printf("send cq: len %d content %s\n", send_cq_name.length(), send_cq_name.c_str());
  // printf("recv cq: len %d content %s\n", recv_cq_name.length(), recv_cq_name.c_str());
  struct ibv_qp_cap qp_cap = cmd.cmd_args.create_qp_input_args.qp_init_attr.cap;
  enum ibv_qp_type qp_type = cmd.cmd_args.create_qp_input_args.qp_init_attr.qp_type;
  int sq_sig_all = cmd.cmd_args.create_qp_input_args.qp_init_attr.sq_sig_all;
  int qp_id = m_ascending_qp_idxs[device_id]++;
  uint32_t qp_num = m_ascending_qp_num++;
  std::string qp_name = "qp_" + std::to_string(device_id) + "_" + std::to_string(qp_id);
  NS_ASSERT(qp_name.length() < 40);
  IbvQP* newQP = m_segment->construct<IbvQP>(qp_name.c_str())();
  newQP->SetIbvInterface(this);
  newQP->SetDevice(0);
  newQP->SetNicIdx(device_id);
  newQP->SetName(qp_name);
  newQP->SetQPCap(qp_cap);
  newQP->SetQPType(qp_type);
  newQP->SetSqSigAll(sq_sig_all);
  newQP->SetQPState(IBV_QPS_RESET);
  newQP->SetQPNum(qp_num);
  newQP->SetNode(m_node);
  newQP->SetSrcIP(m_addr);
  newQP->SetGpusPerServer(m_num_gpus_per_server);
  if(m_enable_gpu_p2p) {
    newQP->EnableP2P();
  } else {
    newQP->DisableP2P();
  }
  // NS3中有对qp和cq是否属于同一个HCA做检查
  if(m_ibv_cqs[device_id].find(send_cq_name)!=m_ibv_cqs[device_id].end()) {
    // NS_ASSERT(m_ibv_cqs[device_id][send_cq_name]->GetDevice()==m_idx_device_map[device_id]);
    newQP->SetSendCQ(m_ibv_cqs[device_id][send_cq_name]);
  } else {
    NS_LOG_ERROR("CreateQP: send_cq not found");
    NS_FATAL_ERROR("CreateQP: send_cq not found");
  }
  if(m_ibv_cqs[device_id].find(recv_cq_name)!=m_ibv_cqs[device_id].end()) {
    // NS_ASSERT(m_ibv_cqs[device_id][recv_cq_name]->GetDevice()==m_idx_device_map[device_id]);
    newQP->SetRecvCQ(m_ibv_cqs[device_id][recv_cq_name]);
  } else {
    NS_LOG_ERROR("CreateQP: recv_cq not found");
    NS_FATAL_ERROR("CreateQP: recv_cq not found");
  }
  // TODO: 设置shm 等等 注册 map 未完待续
  newQP->SetShmSegment(m_segment);
  m_ibv_qps[device_id][qp_name] = Ptr<IbvQP>(newQP);
  m_total_qps[qp_name] = Ptr<IbvQP>(newQP);
  IbvCommandResponse resp;
  resp.cmd_type = IBV_CREATE_QP;
  resp.cmd_id = cmd.cmd_id;
  resp.cmd_reponse.create_qp_output_args.qp_num = qp_num;
  sprintf(resp.cmd_reponse.create_qp_output_args.qp_name,qp_name.c_str());

  return resp;
}

IbvCommandResponse IbvInterface::ModifyQP(IbvCommand &cmd)
{
  std::string qp_name = cmd.cmd_args.modify_qp_input_args.shm_qp_name;
  int mask = cmd.cmd_args.modify_qp_input_args.attr_mask;
  if (m_total_qps.find(qp_name)!=m_total_qps.end()) {
    Ptr<IbvQP> qp = m_total_qps[qp_name];
    if(mask & IBV_QP_STATE) qp->SetQPState(cmd.cmd_args.modify_qp_input_args.attr.qp_state);
    if(mask & IBV_QP_CUR_STATE) qp->SetQPState(cmd.cmd_args.modify_qp_input_args.attr.qp_state);
    if(mask & IBV_QP_DEST_QPN) qp->SetDstQPNum(cmd.cmd_args.modify_qp_input_args.attr.dest_qp_num);
    if(mask & IBV_QP_EN_SQD_ASYNC_NOTIFY) qp->Nop("modify qp (IBV_QP_EN_SQD_ASYNC_NOTIFY)");
    if(mask & IBV_QP_ACCESS_FLAGS) qp->Nop("modify qp (IBV_QP_ACCESS_FLAGS)");
    if(mask & IBV_QP_PKEY_INDEX) qp->Nop("modify qp (IBV_QP_PKEY_INDEX)");
    if(mask & IBV_QP_PORT) qp->Nop("modify qp (IBV_QP_PORT)");
    if(mask & IBV_QP_QKEY) qp->Nop("modify qp (IBV_QP_QKEY)");
    if(mask & IBV_QP_AV) {
      qp->SetAddressAttr(cmd.cmd_args.modify_qp_input_args.attr.ah_attr);
      uint32_t src_ip = qp->GetSrcIP();
      uint32_t dst_ip = qp->GetDstIP();
      // 对于相同的通信两端 (即源地址和目的地址相同) 递增地分配源端口 默认目的端口就是4791 (RoCE v2)
      if(m_ascending_src_port_nums.find(src_ip)==m_ascending_src_port_nums.end() || 
        m_ascending_src_port_nums[src_ip].find(dst_ip)==m_ascending_src_port_nums[src_ip].end()) {
        m_ascending_src_port_nums[src_ip][dst_ip] = SRC_PORT_START_INDEX;
      } else {
        m_ascending_src_port_nums[src_ip][dst_ip]++;
      }
      qp->SetSrcPort(m_ascending_src_port_nums[src_ip][dst_ip]);
      qp->SetDstPort(DEFAULT_DST_PORT);
    }
    if(mask & IBV_QP_PATH_MTU) qp->Nop("modify qp (IBV_QP_PATH_MTU)");
    if(mask & IBV_QP_TIMEOUT) qp->Nop("modify qp (IBV_QP_TIMEOUT)");
    if(mask & IBV_QP_RETRY_CNT) qp->Nop("modify qp (IBV_QP_RETRY_CNT)");
    if(mask & IBV_QP_RNR_RETRY) qp->Nop("modify qp (IBV_QP_RNR_RETRY)");
    if(mask & IBV_QP_RQ_PSN) qp->Nop("modify qp (IBV_QP_RQ_PSN)");  // TODO: 寻找QP中与之对应的变量
    if(mask & IBV_QP_MAX_QP_RD_ATOMIC) NS_ASSERT_MSG(cmd.cmd_args.modify_qp_input_args.attr.max_rd_atomic == 1, "only support max_rd_atomic = 1");
    if(mask & IBV_QP_ALT_PATH) qp->Nop("modify qp (IBV_QP_ALT_PATH)");
    if(mask & IBV_QP_MIN_RNR_TIMER) qp->Nop("modify qp (IBV_QP_MIN_RNR_TIMER)");
    if(mask & IBV_QP_SQ_PSN) qp->Nop("modify qp (IBV_QP_SQ_PSN)");   // TODO: 寻找QP中与之对应的变量
    if(mask & IBV_QP_MAX_DEST_RD_ATOMIC) NS_ASSERT_MSG(cmd.cmd_args.modify_qp_input_args.attr.max_dest_rd_atomic == 1, "only support max_dest_rd_atomic = 1");
    if(mask & IBV_QP_PATH_MIG_STATE) qp->Nop("modify qp (IBV_QP_PATH_MIG_STATE)");
    if(mask & IBV_QP_CAP) qp->SetQPCap(cmd.cmd_args.modify_qp_input_args.attr.cap);
    if (qp->GetQPState()==IBV_QPS_INIT) {
      NS_LOG_INFO(qp_name << " (qp_num=" << qp->GetQPNum() << "): ModifyQP: qp state transforms from RESET to INIT");
    } else if (qp->GetQPState()==IBV_QPS_RTR) {
      NS_LOG_INFO(qp_name << " (qp_num=" << qp->GetQPNum() << "): ModifyQP: qp state transforms from INIT to RTR");
    } else if (qp->GetQPState()==IBV_QPS_RTS) {
      qp->QPCommInit();
      NS_LOG_INFO(qp_name << " (qp_num=" << qp->GetQPNum() << "): ModifyQP: qp state transforms from RTR to RTS");
    }

    IbvCommandResponse resp;
    resp.cmd_type = IBV_MODIFY_QP;
    resp.cmd_id = cmd.cmd_id;
    resp.cmd_reponse.modify_qp_output_args.retval = 0;
    NS_LOG_INFO("ModifyQP (" << qp_name << "): qp state: " << qp->GetQPState());
    return resp;
  } else {
    NS_LOG_ERROR("ModifyQP: qp not found");
    NS_FATAL_ERROR("ModifyQP: qp not found");
  }
}

IbvCommandResponse IbvInterface::DestroyQP(IbvCommand &cmd)
{
  if(m_already_print==false) {
    m_already_print = true;
    printf("IbvInterface of Node %d: %d / %d\n", m_node->GetId(), m_poll_empty_cnt, m_poll_counter);
  }
  std::string qp_name = cmd.cmd_args.destroy_qp_input_args.shm_qp_name;
  if (m_total_qps.find(qp_name)!=m_total_qps.end()) {
    // 默认关闭QP的调试信息的Dump
    // m_total_qps[qp_name]->DumpTrace();
    m_total_qps[qp_name]->PrintTrafficBytes();
    m_total_qps.erase(qp_name);
    IbvCommandResponse resp;
    resp.cmd_type = IBV_DESTROY_QP;
    resp.cmd_id = cmd.cmd_id;
    resp.cmd_reponse.destroy_qp_output_args.retval = 0;
    return resp;
  } else {
    NS_LOG_ERROR("DestroyQP: qp not found");
    NS_FATAL_ERROR("DestroyQP: qp not found");
  }
}

IbvCommandResponse IbvInterface::DestroyCQ(IbvCommand &cmd)
{
  std::string cq_name = cmd.cmd_args.destroy_cq_input_args.shm_cq_name;
  int cq_device_id = -1;
  for(int i=0;i<m_num_qbb_devices;i++) {
    if (m_ibv_cqs[i].find(cq_name)!=m_ibv_cqs[i].end()) {
      cq_device_id = i;
    }
  }
  if(cq_device_id!=-1) {
    // 检查是否存在QP的send_cq/recv_cq绑定在该CQ上
    Ptr<IbvCQ> cq = m_ibv_cqs[cq_device_id][cq_name];
    int has_dependence = 0; 
    for(auto it : m_total_qps) {
      if (it.second->GetSendCQ()==cq || it.second->GetRecvCQ()==cq) {
        has_dependence = 1;
      }
    }
    if(has_dependence==0) {
      m_ibv_cqs[cq_device_id].erase(cq_name);
    }
    IbvCommandResponse resp;
    resp.cmd_id = cmd.cmd_id;
    resp.cmd_type = IBV_DESTROY_CQ;
    resp.cmd_reponse.destroy_cq_output_args.retval = has_dependence;
    return resp;
  } else {
    NS_LOG_ERROR("DestroyCQ: cq not found");
    NS_FATAL_ERROR("DestroyCQ: cq not found");
  } 
}

IbvCommandResponse IbvInterface::PostSend(IbvCommand &cmd)
{
  std::string qp_name = cmd.cmd_args.post_send_input_args.shm_qp_name;
  if (m_total_qps.find(qp_name)!=m_total_qps.end()) {
    Ptr<IbvQP> qp = m_total_qps[qp_name];
    NS_ASSERT_MSG(qp->GetQPState()==IBV_QPS_RTS, "PostSend: qp state is not RTS");
    shm_ibv_send_wr send_wr = cmd.cmd_args.post_send_input_args.wr;
    Simulator::Schedule(NanoSeconds(m_send_latency), &IbvQP::EnqueueSendRequest, qp, send_wr);
    // qp->EnqueueSendRequest(send_wr);
    // if((send_wr.send_flags & IBV_SEND_SIGNALED) == IBV_SEND_SIGNALED) {
    //   NS_LOG_LOGIC("WrEnqueue Counter " << GetPollCounter() << " " << qp->GetDebugName() << " wr_id=" << send_wr.wr_id << ", opcode=" << send_wr.opcode << ", bytes=" << send_wr.total_length);
    // }
    IbvCommandResponse resp;
    resp.cmd_type = IBV_POST_SEND;
    resp.cmd_id = cmd.cmd_id;
    resp.cmd_reponse.post_send_output_args.retval = 0;
    return resp;
  } else {
    NS_LOG_ERROR("PostSend: qp not found");
    NS_FATAL_ERROR("PostSend: qp not found");
  }
}

IbvCommandResponse IbvInterface::PostRecv(IbvCommand &cmd)
{
  std::string qp_name = cmd.cmd_args.post_recv_input_args.shm_qp_name;
  if (m_total_qps.find(qp_name)!=m_total_qps.end()) {
    Ptr<IbvQP> qp = m_total_qps[qp_name];
    shm_ibv_recv_wr recv_wr = cmd.cmd_args.post_recv_input_args.wr;
    qp->EnqueueRecvRequest(recv_wr);
    IbvCommandResponse resp;
    resp.cmd_type = IBV_POST_RECV;
    resp.cmd_id = cmd.cmd_id;
    resp.cmd_reponse.post_send_output_args.retval = 0;
    return resp;
  } else {
    NS_LOG_ERROR("PostRecv: qp not found");
    NS_FATAL_ERROR("PostRecv: qp not found");
  } 
}

IbvCommandResponse IbvInterface::ProvideQPInfo(IbvCommand &cmd)
{
  std::string qp_name = cmd.cmd_args.query_qp_input_args.shm_qp_name;
  if (m_total_qps.find(qp_name)!=m_total_qps.end()) {
    Ptr<IbvQP> qp = m_total_qps[qp_name];
    IbvCommandResponse resp;
    resp.cmd_type = IBV_QUERY_QP; 
    resp.cmd_id = cmd.cmd_id;
    // init_attr
    shm_ibv_qp_init_attr& cur_init_attr = resp.cmd_reponse.query_qp_output_args.init_attr;
    cur_init_attr.cap = qp->GetQPCap();
    cur_init_attr.qp_type = qp->GetQPType();
    cur_init_attr.sq_sig_all = qp->GetSqSigAll();
    // attr
    ibv_qp_attr& cur_attr = resp.cmd_reponse.query_qp_output_args.attr;
    cur_attr.qp_state = qp->GetQPState();
    cur_attr.cur_qp_state = qp->GetQPState();
    cur_attr.dest_qp_num = qp->GetDstQPNum();
    cur_attr.cap = qp->GetQPCap();
    cur_attr.ah_attr = qp->GetAddressAttr();
    cur_attr.max_rd_atomic = 1;
    cur_attr.max_dest_rd_atomic = 1;
    return resp;
  } else {
    NS_LOG_ERROR("QueryQP: qp not found");
    NS_FATAL_ERROR("QueryQP: qp not found");
  }
}

uint64_t IbvInterface::GetBytesLeft()
{
  uint64_t bytes_left = 0;
  for(auto named_qp: m_total_qps) {
    bytes_left += named_qp.second->GetBytesLeft();
  }
  return bytes_left;
}

std::string IbvInterface::GetBytesLeftDetailedInfo() {
  std::string info;
  for(auto named_qp: m_total_qps) {
    info += named_qp.first + ": " + std::to_string(named_qp.second->GetBytesLeft()) + ", ";
  }
  return info.substr(0,info.size()-2);
}
uint64_t IbvInterface::GetPollCounter()
{
  return m_poll_counter;
}
void IbvInterface::EnablePollPrint()
{
  m_poll_print = true;
}
void IbvInterface::StartApplication(void)
{
  char *env_value = getenv("USLEEP_TIME");
  if (env_value == NULL) {
    ipc_sleep_time = 0;
  } else {
    int time_us = std::stoi(env_value);
    ipc_sleep_time = time_us;
  }
  
  printf("IPC Sleep time: %d\n", ipc_sleep_time);
  printf("SegmentName: %s\n",m_segName.c_str());
  printf("SegmentSize: %u\n",m_segSize);
  NS_LOG_LOGIC("SendLatency: " << m_send_latency);
  boost::interprocess::shared_memory_object::remove(m_segName.c_str());
  m_segment = new boost::interprocess::managed_shared_memory(boost::interprocess::create_only, m_segName.c_str(), m_segSize);
  const IbvCommandAllocator alloc_cmd(m_segment->get_segment_manager());
  const IbvCommandResponseAllocator alloc_resp(m_segment->get_segment_manager());
  m_cmd_queue = m_segment->construct<CommandQueue>("cmd_queue")(alloc_cmd);
  m_cmd_resp_queue = m_segment->construct<CommandResponseQueue>("cmd_resp_queue")(alloc_resp);
  CONSTRUCT_MUTEX(m_segment,m_ibv_devices_mutex);
  CONSTRUCT_MUTEX(m_segment,m_ibv_devices_attr_mutex);
  CONSTRUCT_MUTEX(m_segment,m_ibv_num_devices_mutex);
  CONSTRUCT_MUTEX(m_segment,m_ibv_devices_port_attr_mutex);
  CONSTRUCT_MUTEX(m_segment,m_ibv_devices_gid_attr_mutex);
  CONSTRUCT_MUTEX(m_segment,m_cmd_queue_mutex);
  CONSTRUCT_MUTEX(m_segment,m_cmd_resp_queue_mutex);
  UploadDeviceInfo();
  UploadDeviceAttr();
  UploadDevicePortAttr();
  UploadDeviceGidAttr();
  // 此时已经知道了devices的数量
  // 因为CQ和QP都依附于特定的device 所以现在那些vector 的size已经确定
  m_ibv_cqs.resize(m_num_qbb_devices);
  m_ibv_qps.resize(m_num_qbb_devices);
  m_ascending_cq_idxs.resize(m_num_qbb_devices, 0);       // 初始化为0
  m_ascending_qp_idxs.resize(m_num_qbb_devices, 0);       // 初始化为0
  m_ascending_qp_num = 2;                                 // QPN=0/1有特殊用途
  // 轮询CommandQueue 查看上层是否有任务下发
  NS_LOG_INFO("Start to poll command");
  PollCommand();

}
void IbvInterface::StopApplication(void)
{
  // boost::interprocess::shared_memory_object::remove(m_segName.c_str());
}

bool IsNVLinkDev(Ptr<QbbNetDevice> dev)
{
  if(dev->GetDataRate().GetBitRate() < 4e11) {
    return false;
  } else {
    return true;
  }
}

}
