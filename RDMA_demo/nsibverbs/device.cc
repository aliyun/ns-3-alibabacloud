#include "verbs.h"
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


using namespace std;
using namespace boost::interprocess;

boost::interprocess::managed_shared_memory *pSegment = NULL;

extern "C" {
int get_node_id() {
  char *env_value = getenv("NODE_ID");
  if (env_value == NULL) {
    fprintf(stderr, "Environment variable NODE_ID is not set.\n");
    return -1;
  }
  int node_id = std::stoi(env_value);
  return node_id;
}

int get_gpus_per_server() {
  char *env_value = getenv("NUM_GPUS_PER_SERVER");
  if (env_value == NULL) {
    fprintf(stderr, "Environment variable NUM_GPUS_PER_SERVER is not set.\n");
    return -1;
  }
  int num_gpus_per_server = std::stoi(env_value);
  return num_gpus_per_server;  
}
struct ibv_device **ibv_get_device_list_of_node(int *num_devices, int node_id) {
  string shm_name = SHM_NAME_PREFIX + to_string(node_id);
  printf("SHM NAME: %s\n", shm_name.c_str());
  if(!pSegment) {
    pSegment = new managed_shared_memory(open_only, shm_name.c_str());
  }
  string pid_of_node_name = "pid_of_node" + to_string(node_id);
  if(!pSegment->find<int>(pid_of_node_name.c_str()).first) {
    pid_t cur_pid = getpid();
    int *pid_data_ptr = pSegment->construct<int>(pid_of_node_name.c_str())();
    INFO(NCCL_INIT,"construct pid %d for node %d", cur_pid, node_id);
    *pid_data_ptr = cur_pid;
  }
  cmd_queue = pSegment->find<CommandQueue>("cmd_queue").first;
  cmd_resp_queue = pSegment->find<CommandResponseQueue>("cmd_resp_queue").first;
  cmd_queue_mutex = pSegment->find<boost::interprocess::interprocess_mutex>("m_cmd_queue_mutex").first;
  cmd_resp_queue_mutex = pSegment->find<boost::interprocess::interprocess_mutex>("m_cmd_resp_queue_mutex").first;
  if(cmd_queue) {
    printf("Good: can find cmd_queue in shared memory %s\n", shm_name.c_str());
  } else {
    printf("Error: cannot find cmd_queue in shared memory %s\n", shm_name.c_str());
  }
  if(cmd_resp_queue) {
    printf("Good: can find cmd_resp_queue in shared memory %s\n", shm_name.c_str());
  } else {
    printf("Error: cannot find cmd_resp_queue in shared memory %s\n", shm_name.c_str());
  }
  // 只读不加锁
  printf("before find num_devices\n");
  int* ptr_num_devices = pSegment->find<int>("num_devices").first;
  printf("after find num_devices\n");
  if(ptr_num_devices) {
    printf("before memcpy\n");
    memcpy(num_devices, ptr_num_devices, sizeof(int));
    printf("after memcpy\n");
  } else {
    fprintf(stderr,"cannot find num_devices in shared memory\n");
  }
  
  /* malloc的区域在ibv_free_device_list中释放 */
  struct ibv_device **devices = (struct ibv_device **)malloc((*num_devices)*sizeof(struct ibv_device*));
  if(!devices) {
    fprintf(stderr,"Error: cannot malloc memory for devices\n");
    exit(-1);
  } 
  
  for(int i=0;i<*num_devices;i++) {
    string device_name = "qbb_net_device_" + to_string(i);
    devices[i] = (struct ibv_device*)malloc(sizeof(struct ibv_device));
    struct ibv_device *tmp = pSegment->find<ibv_device>(device_name.c_str()).first;
    if(tmp) {
      memcpy(devices[i],tmp,sizeof(struct ibv_device));
    } else {
      fprintf(stderr,"Error: cannot find cmd_resp_queue in shared memory %s\n", device_name.c_str());
      exit(-1);
    }
    
  }
  printf("%s:%d return\n", __func__, __LINE__);
  return devices;
}
struct ibv_device **ibv_get_device_list(int *num_devices) {
  // mylibibverbs 中 必须需要传入nodeID才能获取对应的信息
  // 当前实现是要求必须设置环境变量NODE_ID
  // 此处解析NODE_ID环境变量的值作为node_id
  int node_id = get_node_id();
  return ibv_get_device_list_of_node(num_devices, node_id);
}
void ibv_free_device_list(struct ibv_device **list) {
  // TODO: 释放资源
  free(list);
  list = NULL;
}

const char *ibv_get_device_name(struct ibv_device *device) {
  return device->name;
}
int extract_device_id(string device_name, string name_prefix) {
  return stoi(device_name.substr(name_prefix.size()));
}
struct ibv_context *ibv_open_device(struct ibv_device *device) {
  // TODO: 释放资源
  ibv_context *context = (ibv_context*)malloc(sizeof(ibv_context));
  memset(context, 0, sizeof(ibv_context));
  context->ops.post_send = shm_ibv_post_send;
  context->ops.post_recv = shm_ibv_post_recv;
  context->ops.poll_cq = shm_ibv_poll_cq;
  context->device = (struct ibv_device*)malloc(sizeof(struct ibv_device));
  memcpy(context->device, device, sizeof(struct ibv_device));
  context->cmd_fd = extract_device_id(device->name, "ns3_qbbdev_");
  printf("%s:%d return\n", __func__, __LINE__);
  return context;
}
int ibv_close_device(ibv_context *context)
{
  free(context);
  context = NULL;
  return 0;
}
int ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr)
{
  // 只读 无需加锁
  int device_id = context->cmd_fd;
  // return 0 on success, -1 on error
  std::string device_attr_name = "qbb_net_device_attr_" + std::to_string(device_id);
  struct ibv_device_attr * tmp_device_attr = pSegment->find<ibv_device_attr>(device_attr_name.c_str()).first;

  if(tmp_device_attr) {
    memcpy(device_attr, tmp_device_attr, sizeof(ibv_device_attr));
    printf("%s:%d return\n", __func__, __LINE__);
    return 0;
  } else {
    return -1;
  }
}
int ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr) {
  int device_id = context->cmd_fd;
  std::string port_attr_name = "device_" + std::to_string(device_id) + "_port_attr";
  struct ibv_port_attr * tmp_port_attr = pSegment->find<ibv_port_attr>(port_attr_name.c_str()).first;
  if(tmp_port_attr) {
    memcpy(port_attr,tmp_port_attr, sizeof(ibv_port_attr));
    printf("%s:%d return\n", __func__, __LINE__);
    return 0;
  } else {
    return -1;
  };
}
int ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid) {
  int device_id = context->cmd_fd;
  std::string gid_name = "device_" + std::to_string(device_id) + "_gid";
  union ibv_gid * tmp_gid = pSegment->find<union ibv_gid>(gid_name.c_str()).first;
  if(tmp_gid) {
    memcpy(gid, tmp_gid, sizeof(union ibv_gid));
    // printf("%s:%d return\n", __func__, __LINE__);
    return 0;
  } else {
    return -1;
  }
}

int ibv_fork_init(void) {
  return 0;
}

}