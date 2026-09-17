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
typedef boost::interprocess::allocator<shm_ibv_wc, boost::interprocess::managed_shared_memory::segment_manager> CQEAllocator;
typedef boost::interprocess::vector<shm_ibv_wc, CQEAllocator> CQEQueue;
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, 
        void *cq_context, struct ibv_comp_channel *channel, int comp_vector) {
  int device_id = context->cmd_fd;
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_id = ascending_cmd_id++;
  cmd.cmd_type = IBV_CREATE_CQ;
  cmd.cmd_args.create_cq_input_args.device_id = device_id;
  cmd.cmd_args.create_cq_input_args.cqe = cqe;
  cmd.cmd_args.create_cq_input_args.comp_vector = comp_vector;
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp,cmd.cmd_id);
  if (re1 && re2) {
    char *cq_name = (char *)malloc(40);
    strncpy(cq_name, resp.cmd_reponse.create_cq_output_args.cq_name,NAME_LEN);
    ibv_cq *cq = (ibv_cq *) malloc(sizeof(ibv_cq));
    // memcpy(cq,0,sizeof(ibv_cq));
    cq->context = context;
    cq->channel = NULL;
    cq->cq_context = (void*)cq_name;
    cq->cqe = cqe;
    pthread_mutex_init(&cq->mutex,nullptr);
    pthread_cond_init(&cq->cond,nullptr);
    return cq;
  } else {
    fprintf(stderr, "ibv_create_cq: failed to create cq\n");
    return NULL;
  }
}

int ibv_destroy_cq(ibv_cq *cq)
{
  if(!cq) {
    fprintf(stderr, "ibv_destroy_cq: cq is NULL\n");
    exit(-1);
  }
  IbvCommand cmd;
  IbvCommandResponse resp;
  cmd.cmd_type = IBV_DESTROY_CQ;
  cmd.cmd_id = ascending_cmd_id++;
  strncpy(cmd.cmd_args.destroy_cq_input_args.shm_cq_name, (char*)cq->cq_context,NAME_LEN); 
  // INFO(NCCL_NET, "ibv_destroy_cq: cmd_id = %u, cq = %s\n", cmd.cmd_id, cmd.cmd_args.destroy_cq_input_args.shm_cq_name);
  bool re1 = SendIbvCommand(cmd);
  bool re2 = RecvIbvCommandResponse(resp, cmd.cmd_id);
  if (re1 && re2) {
    if(resp.cmd_reponse.destroy_cq_output_args.retval == 0) {
      free(cq);
      return 0;
    } else {
      fprintf(stderr, "ibv_destroy_cq: there is QP that still has the specified CQ associated with this CQ\n");
      return -1;
    }
  } else {
    if(!re1)  fprintf(stderr, "ibv_destroy_cq: failed to send ibv command\n");
    if(!re2)  fprintf(stderr, "ibv_destroy_cq: failed to receive ibv command response\n");
    return -1;
  }
}

int shm_ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
  // 根据CQ的cq_name拼接得到其在共享内存区域存放CQE的vector的名字
  // 打开它
  std::string cq_name((char *)cq->cq_context);
  std::string cq_container_name = cq_name + "_container";
  std::string cq_container_mutex_name = cq_name + "_container_mutex";
  CQEQueue * cq_container = pSegment->find<CQEQueue>(cq_container_name.c_str()).first;
  boost::interprocess::interprocess_mutex* cq_container_mutex = pSegment->find<boost::interprocess::interprocess_mutex>(cq_container_mutex_name.c_str()).first; 
  boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock_m_cq_container_mutex(*cq_container_mutex);
  int num_polled = std::min(num_entries, (int)cq_container->size());

  for(int i=0; i<num_polled; i++) {
    shm_ibv_wc shm_wc = cq_container->front();
    wc[i].wr_id = shm_wc.wr_id;
    wc[i].status = shm_wc.status;
    wc[i].opcode = shm_wc.opcode;
    wc[i].vendor_err = shm_wc.vendor_err;
    wc[i].byte_len = shm_wc.byte_len;
    wc[i].imm_data = shm_wc.imm_data;
    wc[i].qp_num = shm_wc.qp_num;
    wc[i].src_qp = shm_wc.src_qp;
    wc[i].wc_flags = shm_wc.wc_flags;
    wc[i].pkey_index = shm_wc.pkey_index;
    wc[i].slid = shm_wc.slid;
    wc[i].sl = shm_wc.sl;
    wc[i].dlid_path_bits = shm_wc.dlid_path_bits;
    cq_container->erase(cq_container->begin());
  }
  return num_polled;
}

int shm_generate_cqe(struct ibv_cq *cq, shm_ibv_send_wr wr, uint32_t local_qpn) {
  // 根据CQ的cq_name拼接得到其在共享内存区域存放CQE的vector的名字
  // 打开它
  std::string cq_name((char *)cq->cq_context);
  std::string cq_container_name = cq_name + "_container";
  std::string cq_container_mutex_name = cq_name + "_container_mutex";
  CQEQueue * cq_container = pSegment->find<CQEQueue>(cq_container_name.c_str()).first;
  boost::interprocess::interprocess_mutex* cq_container_mutex = pSegment->find<boost::interprocess::interprocess_mutex>(cq_container_mutex_name.c_str()).first; 
  boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock_m_cq_container_mutex(*cq_container_mutex);
  struct shm_ibv_wc wc;
  memset(&wc, 0, sizeof(wc));
  wc.wr_id = wr.wr_id;
  wc.status = IBV_WC_SUCCESS;
  wc.qp_num = local_qpn;
  wc.src_qp = 0;
  wc.byte_len = wr.total_length;
  wc.imm_data = wr.imm_data; 
  switch (wr.opcode) {
    case IBV_WR_RDMA_WRITE:
      wc.opcode = IBV_WC_RDMA_WRITE;
      break;
    default:
      fprintf(stderr, "This operation only support RDMA_WRITE in ncclIbIflush\n");
  }
  cq_container->push_back(wc);
  return 0;
}
}
