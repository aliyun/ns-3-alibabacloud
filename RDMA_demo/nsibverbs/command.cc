#include "command.h"
#include "debug.h"
#include <sys/time.h>
#include <fstream>
uint32_t ascending_cmd_id = 0;
std::vector<int64_t> pollTimeVector;
std::ofstream outFile("pollTimeVector.txt");
CommandQueue *cmd_queue = NULL;
CommandResponseQueue *cmd_resp_queue = NULL;
boost::interprocess::interprocess_mutex * cmd_queue_mutex = NULL;
boost::interprocess::interprocess_mutex * cmd_resp_queue_mutex = NULL;
#define USLEEP_TIME 0

extern "C" {
char *GetCurrentTime() {
	struct timeval tv;
	struct tm *tm_info;
	char time_str[200];
	char total_time_str[240];

	// 获取当前时间
	if (gettimeofday(&tv, NULL) != 0) {
		printf("Error in gettimeofday");
		return NULL;
	}
	tm_info = localtime(&tv.tv_sec);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
	// 打印时间，包括微秒
	sprintf(total_time_str, "[%s.%06ld]", time_str, tv.tv_usec);
	return total_time_str;
}

int64_t GetSimTime(int flag) {
  char *env_value = getenv("SOCKET_FD");
  if (env_value == NULL) {
    fprintf(stderr, "Environment variable SOCKET_FD is not set.\n");
    return -1;
  }
  int sock = std::stoi(env_value);
  // 发送数据到服务器
  int message = flag;
  write(sock, &message, sizeof(message));
  // 接收服务器的响应
  int64_t time_ns = 0;
  int valread = read(sock, &time_ns, sizeof(time_ns));
  if (valread == sizeof(time_ns)) {
    return time_ns;
  } else {
    printf("read failed");
    return -1;
  }
}
int get_usleep_time() {
  char *env_value = getenv("USLEEP_TIME");
  if (env_value == NULL) {
    return 0;
  } else {
    int time_us = std::stoi(env_value);
    return time_us;
  }
}
bool SendIbvCommand(IbvCommand &cmd) {
  // INFO(NCCL_NET, "Node %d,Cmd %u SendIbvCommand\n", get_node_id(), cmd.cmd_id);
  if(cmd_queue && cmd_queue_mutex) {
    usleep(get_usleep_time());
    LOCK_MUTEX(cmd_queue);
    cmd_queue->push_back(cmd);
    return true;
  } else {
    printf("Error: pointer is NULL: %d %d\n",cmd_queue==NULL, cmd_queue_mutex==NULL);
    return false;
  }
}

bool TryPopCommandResponse(IbvCommandResponse &cmd_resp)
{
  if (cmd_resp_queue->empty()) {
    return false;
  }
  else {
    LOCK_MUTEX(cmd_resp_queue);
    cmd_resp = cmd_resp_queue->front();
    cmd_resp_queue->erase(cmd_resp_queue->begin());
    return true;
  }
}
bool RecvIbvCommandResponse(IbvCommandResponse &cmd_resp, uint32_t cmd_id) {
  if(cmd_resp_queue && cmd_resp_queue_mutex) {
    auto start_time = std::chrono::high_resolution_clock::now();
    int counter = 0;
    while (true)
    {
      // 超时检查
      auto current_time = std::chrono::high_resolution_clock::now();
      auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
      if(elapsed_time >= POLL_TIMEOUT_VALUE) {
        printf("Error: timeout\n");
        return false;
      }
      bool result = TryPopCommandResponse(cmd_resp);
      counter ++;
      if(result) {
        if(cmd_resp.cmd_id == cmd_id) {
          // INFO(NCCL_NET, "Node %d,Cmd %u RecvIbvCommandResponse\n", get_node_id(), cmd_resp.cmd_id);
          if(get_node_id() == 0){
            pollTimeVector.push_back(counter);
            if(pollTimeVector.size() >= 500) {
              if (!outFile.is_open()) {
                  std::cerr << "无法打开文件 pollTimeVector.txt" << std::endl;
                  return 1;
              }
              // 将向量中的每个元素逐行写入文件
              for (const auto& line : pollTimeVector) {
                  outFile << line << std::endl;
              }
              pollTimeVector.clear();
            }
          }
          return true;
        } else {
          printf("Error: Node %d,Cmd %u cmd_id not match: %d %d\n",get_node_id(), cmd_resp.cmd_id, cmd_resp.cmd_id, cmd_id);
          return false;
        }
      }
    }
  } else {
    printf("Error: pointer is NULL: %d %d\n",cmd_resp_queue==NULL, cmd_resp_queue_mutex==NULL);
    return false;
  }
} 
}
