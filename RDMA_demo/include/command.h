#ifndef __COMMAND_H__
#define __COMMAND_H__

#include <stdint.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <vector>
// NOTE(RDMA_demo): 原 NCCL 版本此处为 #include "core.h"，它会进一步拉入 nccl.h / checks.h / cudawrap.h / alloc.h
// 以及 CUDA 等重量级依赖。command.h 本身只用到 verbs.h 的类型与下列标准系统头，
// 故用精简系统头替换 core.h，使 nsibverbs 能脱离 NCCL 主体独立编译。
#include <unistd.h>
#include <stdlib.h>
#include "verbs.h"
#define POLL_TIMEOUT_VALUE 4000

struct IbvCommand;
struct IbvCommandResponse;
typedef boost::interprocess::allocator<IbvCommand, boost::interprocess::managed_shared_memory::segment_manager> IbvCommandAllocator;
typedef boost::interprocess::allocator<IbvCommandResponse, boost::interprocess::managed_shared_memory::segment_manager> IbvCommandResponseAllocator;
typedef boost::interprocess::vector<IbvCommand, IbvCommandAllocator> CommandQueue;
typedef boost::interprocess::vector<IbvCommandResponse, IbvCommandResponseAllocator> CommandResponseQueue;
extern uint32_t ascending_cmd_id;
extern std::vector<int64_t> pollTimeVector;
extern std::ofstream outFile;
extern CommandQueue *cmd_queue;
extern CommandResponseQueue *cmd_resp_queue;
extern boost::interprocess::interprocess_mutex * cmd_queue_mutex;
extern boost::interprocess::interprocess_mutex * cmd_resp_queue_mutex;

extern "C" {
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
char *GetCurrentTime();
int64_t GetSimTime(int flag);

bool SendIbvCommand(IbvCommand &cmd);
bool TryPopCommandResponse(IbvCommandResponse &cmd_resp);
bool RecvIbvCommandResponse(IbvCommandResponse &cmd_resp, uint32_t cmd_id);
}
#endif  // __COMMAND_H__