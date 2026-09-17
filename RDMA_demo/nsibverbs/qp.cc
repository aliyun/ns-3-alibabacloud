#include "verbs.h"
#include "debug.h"
#include "command.h"
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <stdint.h>


using namespace std;
using namespace boost::interprocess;
extern "C" {
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
			     struct ibv_qp_init_attr *qp_init_attr) {
  if((!pd) || (!qp_init_attr)) {
    WARN("ibv_create_qp: pd or qp_init_attr is NULL");
    exit(-1);
  }
  if(pd->context != qp_init_attr->send_cq->context ||
     pd->context != qp_init_attr->recv_cq->context) {
    WARN("ibv_create_qp: send_cq and recv_cq must be from the same context");
    exit(-1);
  }
  int device_id = pd->context->cmd_fd;
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_id = ascending_cmd_id++;
  cmd.cmd_type = IBV_CREATE_QP;
  cmd.cmd_args.create_qp_input_args.device_id = device_id;
  strncpy(cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_send_cq_name, (char*)qp_init_attr->send_cq->cq_context,NAME_LEN);
  strncpy(cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_recv_cq_name, (char*)qp_init_attr->recv_cq->cq_context,NAME_LEN);
  // printf("rv: %s\n",(char*)qp_init_attr->send_cq->cq_context);
  // printf("rv: %s\n",(char*)qp_init_attr->recv_cq->cq_context);
  // printf("shm_send_cq_name: %s\n", cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_send_cq_name);
  // printf("shm_recv_cq_name: %s\n", cmd.cmd_args.create_qp_input_args.qp_init_attr.shm_recv_cq_name);
  cmd.cmd_args.create_qp_input_args.qp_init_attr.cap = qp_init_attr->cap;
  cmd.cmd_args.create_qp_input_args.qp_init_attr.qp_type = qp_init_attr->qp_type;
  cmd.cmd_args.create_qp_input_args.qp_init_attr.sq_sig_all = qp_init_attr->sq_sig_all;
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
  if (re1 && re2) {
    char *qp_name = (char *)malloc(40);
    strncpy(qp_name, resp.cmd_reponse.create_qp_output_args.qp_name,NAME_LEN);
    ibv_qp *qp = (ibv_qp *) malloc(sizeof(ibv_qp));
    
    qp->context = pd->context;
    qp->qp_context = (void*)qp_name;
    qp->pd = pd;
    qp->send_cq = qp_init_attr->send_cq;
    qp->recv_cq = qp_init_attr->recv_cq;
    qp->srq = qp_init_attr->srq;
    qp->qp_num = resp.cmd_reponse.create_qp_output_args.qp_num;
    qp->state = IBV_QPS_RESET;
    qp->qp_type = qp_init_attr->qp_type;
    qp->handle = -1;    //  initialized to -1 means remote_node_id
    pthread_mutex_init(&qp->mutex,nullptr);
    pthread_cond_init(&qp->cond,nullptr);
    return qp;
  } else {
    WARN("ibv_create_qp: failed to create qp");
    return NULL;
  }
}       

uint32_t cal_sge_total_len(struct ibv_sge *sg_list, 
											int num_sge)
{
  uint32_t total_len = 0;
  for(int i=0;i<num_sge;i++) {
    total_len += sg_list[i].length;
  }
  return total_len;
}

// 从 /proc/<pid>/mem 中读取数据	
ssize_t read_remote_memory(pid_t pid, void *addr, void *buffer, size_t size) {	
  char path[50];	
  sprintf(path, "/proc/%d/mem", pid);	
  int fd = open(path, O_RDONLY);	
  if (fd == -1) {	
    WARN("fail to open /proc/pid/mem");	
    return -1;	
  }	
  ssize_t bytes_read = pread(fd, buffer, size, (uintptr_t)addr);	
  if (bytes_read == -1) {	
    WARN("fail to read %lu bytes from remote memory");	
  } else {	
    INFO(NCCL_INIT, "success to read %u bytes", bytes_read);	
  }	
  close(fd);	
  return bytes_read;	
}	
// 写入数据到 /proc/<pid>/mem	
ssize_t write_remote_memory(pid_t pid, void *addr, const void *buffer, size_t size) {	
  char path[50];	
  sprintf(path, "/proc/%d/mem", pid);	
  int fd = open(path, O_WRONLY);	
  if (fd == -1) {	
    WARN("fail to open /proc/pid/mem");	
    return -1;	
  }	
  ssize_t bytes_written = pwrite(fd, buffer, size, (uintptr_t)addr);	
  if (bytes_written == -1) {	
    WARN("fail to write size %lu to remote memory", size);	
  } else {	
    // INFO(NCCL_INIT, "success to write %u bytes", bytes_written);	
  }	
  close(fd);	
  return bytes_written;	
}	
ssize_t post_send_data_transport(ibv_qp *qp, ibv_send_wr *wr) {	
  if(qp->handle == -1) {	
    return -1;	
  }	
  // 根据remote_node_id 找到对端节点的共享内存 从而获取对端节点的pid	
  int remote_node_id = qp->handle;	
  string remote_seg_name = SHM_NAME_PREFIX + to_string(remote_node_id);	
  string pid_name = "pid_of_node" + to_string(remote_node_id);	
  boost::interprocess::managed_shared_memory remote_segment(open_only, remote_seg_name.c_str());	
  int *remote_pid_ptr = remote_segment.find<int>(pid_name.c_str()).first;	
  if(!remote_pid_ptr) {	
    WARN("post_send_data_transport: failed to find remote pid of node %d", remote_node_id);	
    return -1;	
  }	
  pid_t remote_pid = *remote_pid_ptr;	
  uintptr_t remote_addr = wr->wr.rdma.remote_addr;	
  uintptr_t local_addr = wr->sg_list[0].addr;	
  	
  if(wr->opcode == IBV_WR_RDMA_READ) {	
    return read_remote_memory(remote_pid, (void*)remote_addr, (void*)local_addr, wr->sg_list[0].length);	
  } else if (wr->opcode == IBV_WR_RDMA_WRITE || wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM) {	
    return write_remote_memory(remote_pid, (void*)remote_addr, (void*)local_addr, wr->sg_list[0].length);	
  }	
}

int shm_ibv_post_send(ibv_qp *qp, ibv_send_wr *wr, ibv_send_wr **bad_wr)
{
  if((!qp) || (!wr)) {
    WARN("shm_ibv_post_send: qp or wr is NULL");
    return -1;
  }
  for(ibv_send_wr *cur_wr = wr;cur_wr;cur_wr=cur_wr->next) {
    // if(cur_wr->opcode == IBV_WR_RDMA_READ) {
    //   post_send_data_transport(qp, cur_wr);
    // }
    bool enable_p2p = (strcmp(getenv("SIMU_ENABLE_GPU_P2P"), "true") == 0);
    // printf("enable P2P: %d", enable_p2p);
    bool is_postfifo = cur_wr->opcode == IBV_WR_RDMA_WRITE && cur_wr->imm_data == POST_FIFO_IMM && cal_sge_total_len(cur_wr->sg_list,cur_wr->num_sge) == 64;
    bool is_src_dst_in_same_server = get_node_id() / get_gpus_per_server() == qp->handle / get_gpus_per_server();
    if(enable_p2p && is_postfifo && is_src_dst_in_same_server) {
      // printf("Post Fifo in same server\n");
      post_send_data_transport(qp, cur_wr);
      if((cur_wr->send_flags & IBV_SEND_SIGNALED) == IBV_SEND_SIGNALED) {	
        shm_ibv_send_wr shm_wr;	
        shm_wr.wr_id = cur_wr->wr_id;	
        shm_wr.opcode = cur_wr->opcode;
        shm_wr.imm_data = cur_wr->imm_data;	
        shm_wr.total_length = cal_sge_total_len(cur_wr->sg_list,cur_wr->num_sge);	
        shm_wr.send_flags = cur_wr->send_flags;	
        shm_generate_cqe(qp->send_cq, shm_wr, qp->qp_num);
      }	
      return 0;	
    }

    // INFO(NCCL_NET, "Calling ibv_post_send %u bytes", cal_sge_total_len(cur_wr->sg_list,cur_wr->num_sge));
    IbvCommand cmd;
    IbvCommandResponse resp;
    cmd.cmd_type = IBV_POST_SEND;
    cmd.cmd_id = ascending_cmd_id++;
    strncpy(cmd.cmd_args.post_send_input_args.shm_qp_name, (char*)qp->qp_context,NAME_LEN);
    cmd.cmd_args.post_send_input_args.wr.wr_id = cur_wr->wr_id;
    cmd.cmd_args.post_send_input_args.wr.opcode = cur_wr->opcode;
    cmd.cmd_args.post_send_input_args.wr.send_flags = cur_wr->send_flags;
    cmd.cmd_args.post_send_input_args.wr.imm_data = cur_wr->imm_data;
    cmd.cmd_args.post_send_input_args.wr.total_length = cal_sge_total_len(cur_wr->sg_list,cur_wr->num_sge);
    cmd.cmd_args.post_send_input_args.wr.remote_addr = cur_wr->wr.rdma.remote_addr;
    if(is_postfifo) {
      cmd.cmd_args.post_send_input_args.wr.is_post_fifo = 1;
      struct ibv_send_fifo *post_fifo = (struct ibv_send_fifo *)cur_wr->sg_list[0].addr;
      
      cmd.cmd_args.post_send_input_args.wr.post_fifo_addr     = post_fifo->addr;
      cmd.cmd_args.post_send_input_args.wr.post_fifo_size     = post_fifo->size;
      cmd.cmd_args.post_send_input_args.wr.post_fifo_rkeys[0] = post_fifo->rkeys[0];
      cmd.cmd_args.post_send_input_args.wr.post_fifo_rkeys[1] = post_fifo->rkeys[1];
      cmd.cmd_args.post_send_input_args.wr.post_fifo_nreqs    = post_fifo->nreqs;
      cmd.cmd_args.post_send_input_args.wr.post_fifo_tag      = post_fifo->tag;
      cmd.cmd_args.post_send_input_args.wr.post_fifo_idx      = post_fifo->idx;
      cmd.cmd_args.post_send_input_args.wr.post_fifo_wr_id      = post_fifo->wr_id;
      // printf("ncclIbSendFifo.wr_id =  %d\n", post_fifo->wr_id);
    } else {
      cmd.cmd_args.post_send_input_args.wr.is_post_fifo = 0;
    }
    bool re1 = SendIbvCommand(cmd);
    // bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
    bool re2 = true;
    if (!(re1&&re2)){
      if(!re1)  WARN("ibv_post_send: failed to send ibv command");
      if(!re2)  WARN("ibv_post_send: failed to receive ibv command response");
      *bad_wr = cur_wr;
      return -1;
    }
  }
  return 0;
}

int shm_ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
				struct ibv_recv_wr **bad_wr)
{
  if((!qp) || (!wr)) {
    WARN("shm_ibv_post_recv: qp or wr is NULL");
    return -1;
  }
  for(ibv_recv_wr *cur_wr = wr;cur_wr;cur_wr=cur_wr->next) {
    // INFO(NCCL_NET, "Calling ibv_post_recv");
    IbvCommand cmd;
    IbvCommandResponse resp;
    cmd.cmd_type= IBV_POST_RECV;
    cmd.cmd_id = ascending_cmd_id++;
    strncpy(cmd.cmd_args.post_recv_input_args.shm_qp_name, (char*)qp->qp_context,NAME_LEN);
    cmd.cmd_args.post_recv_input_args.wr.wr_id = cur_wr->wr_id;
    cmd.cmd_args.post_recv_input_args.wr.total_length = cal_sge_total_len(cur_wr->sg_list, cur_wr->num_sge);
    bool re1 = SendIbvCommand(cmd);
    // bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
    bool re2 = true;
    if (!(re1&&re2)){
      if(!re1)  WARN("ibv_post_recv: failed to send ibv command");
      if(!re2)  WARN("ibv_post_recv: failed to receive ibv command response");
      *bad_wr = cur_wr;
      return -1;
    }
  }
  return 0;
}

int ibv_modify_qp(ibv_qp *qp, ibv_qp_attr *attr, int attr_mask)
{
  if((!qp) || (!attr)) {
    fprintf(stderr, "ibv_modify_qp: qp or attr is NULL\n");
    exit(-1);
  }
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_type = IBV_MODIFY_QP;
  cmd.cmd_id = ascending_cmd_id++;
  cmd.cmd_args.modify_qp_input_args.attr = *attr;
  // memcpy(&cmd.cmd_args.modify_qp_input_args.attr, attr, sizeof(ibv_qp_attr));
  cmd.cmd_args.modify_qp_input_args.attr_mask = attr_mask;
  strncpy(cmd.cmd_args.modify_qp_input_args.shm_qp_name, (char*)qp->qp_context,NAME_LEN); 
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
  if (re1 && re2) {
    // 令 qp->handle 存储对端node id
    if(attr_mask & IBV_QP_AV) {
      uint32_t remote_node_id = static_cast<uint32_t>((attr->ah_attr.grh.dgid.global.interface_id & 0xFFFFFFFF00000000) >> 32);  // remote NodeID 高32位
      qp->handle = remote_node_id;
    }
    return 0;
  } else {
    if(!re1)  fprintf(stderr, "ibv_modify_qp: failed to send ibv command\n");
    if(!re2)  fprintf(stderr, "ibv_modify_qp: failed to receive ibv command response\n");
    return -1;
  }
}

int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		 int attr_mask,
		 struct ibv_qp_init_attr *init_attr) {
  if(!qp) {
    fprintf(stderr, "ibv_query_qp: qp is NULL\n");
    return -1;
  }
  if((!attr) || (!init_attr)) {
    fprintf(stderr, "ibv_query_qp: attr or init_attr is NULL\n");
    return -1;
  }
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_type = IBV_QUERY_QP;
  cmd.cmd_id = ascending_cmd_id++;
  cmd.cmd_args.query_qp_input_args.attr_mask = attr_mask;
  strncpy(cmd.cmd_args.query_qp_input_args.shm_qp_name, (char*)qp->qp_context,NAME_LEN);
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp,cmd.cmd_id);
  if (re1 && re2) {
    memcpy(attr, &resp.cmd_reponse.query_qp_output_args.attr, sizeof(ibv_qp_attr));
    init_attr->qp_context = qp->qp_context;
    init_attr->send_cq = qp->send_cq;
    init_attr->recv_cq = qp->recv_cq;
    init_attr->srq = qp->srq;
    init_attr->cap = resp.cmd_reponse.query_qp_output_args.init_attr.cap;
    init_attr->qp_type = qp->qp_type;
    init_attr->sq_sig_all = resp.cmd_reponse.query_qp_output_args.init_attr.sq_sig_all;
    return 0; 
  } else {
    if(!re1) fprintf(stderr, "ibv_query_qp: failed to send ibv command\n");
    if(!re2) fprintf(stderr, "ibv_query_qp: failed to receive ibv command response\n");
    return -1;
  }
}
int ibv_destroy_qp(ibv_qp *qp)
{
  if(!qp) {
    fprintf(stderr, "ibv_destroy_qp: qp is NULL\n");
    exit(-1);
  }
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_type = IBV_DESTROY_QP;
  cmd.cmd_id = ascending_cmd_id++;
  strncpy(cmd.cmd_args.destroy_qp_input_args.shm_qp_name, (char*)qp->qp_context,NAME_LEN); 
  // INFO(NCCL_NET, "ibv_destroy_qp: cmd_id = %u, qp = %s\n", cmd.cmd_id, cmd.cmd_args.destroy_qp_input_args.shm_qp_name);
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
  if (re1 && re2) {
    free(qp);
    return 0;
  } else {
    if(!re1)  fprintf(stderr, "ibv_destroy_qp: failed to send ibv command\n");
    if(!re2)  fprintf(stderr, "ibv_destroy_qp: failed to receive ibv command response\n");
    return -1;
  }
}
}
