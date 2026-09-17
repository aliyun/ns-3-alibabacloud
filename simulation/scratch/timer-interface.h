#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
// socket
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
// real time print
#include <iomanip>
#include <chrono>
#include <ctime>
// ns3 schedule timer
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#define SERVER_PORT         13000
#define QUERY_TIME_FLAG     222
#define QUERY_CPUTIME_FLAG  220


bool enable_poll_print = false;
ApplicationContainer apps;
std::vector<int> timerSocketsVec;
namespace ns3 {
int set_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags < 0) {
    perror("fcntl (F_GETFL)");
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, flags) < 0) {
    perror("fcntl (F_SETFL)");
    return -1;
  }
  return 0;
}
int InitializeListeningSocket(int socketId)
{
  int server_fd;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  // Create socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    NS_FATAL_ERROR("socket failed");
  }
  // Set SO_REUSEADDR option
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    NS_FATAL_ERROR("setsockopt failed");
  }
  // Bind socket to IP address and port
  address.sin_family = AF_INET;
  address.sin_port = htons(SERVER_PORT + socketId);
  address.sin_addr.s_addr = INADDR_ANY;
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    close(server_fd);
    perror("bind");
    NS_FATAL_ERROR("bind failed port = " << SERVER_PORT + socketId);
  }

  // Listen for connections
  if (listen(server_fd, 3) < 0) {
    close(server_fd);
    NS_FATAL_ERROR("listen failed");
  }
  printf("Node %d: Server is listening on port %d\n", socketId, SERVER_PORT + socketId);

  return server_fd;
}

int AcceptConnection(int server_fd)
{
  int new_socket;
  // Accept connection
  if ((new_socket = accept(server_fd, NULL, NULL)) < 0) {
    close(server_fd);
    NS_FATAL_ERROR("accept failed");
  }
  close(server_fd);
  set_nonblocking(new_socket);
  return new_socket;
}
void PollTimeQuery(int timer_sock)
{
  int receivedInt = -1;
  if(read(timer_sock, &receivedInt, sizeof(receivedInt))==sizeof(receivedInt)) {
    int64_t time_ns = Simulator::Now().GetNanoSeconds();
    write(timer_sock, &time_ns, sizeof(time_ns));
    // Get current real time
    auto now = std::chrono::system_clock::now();
    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm* localTime = std::localtime(&currentTime);
    if (receivedInt) {
      printf("%2d:%2d:%2d timer_sock %d Received time query flag = %d, sending time %ld\n", 
              localTime->tm_hour, localTime->tm_min, localTime->tm_sec, timer_sock, receivedInt, time_ns);
    }
    if (receivedInt == QUERY_TIME_FLAG) {
      if (false && (!enable_poll_print)) {
        LogComponentEnable("QbbNetDevice", (LogLevel)(LOG_INFO | LOG_FUNCTION | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
        enable_poll_print = true;
        // Let all IbvInterfaces print their PollCommand logs
        for(uint32_t i = 0; i < apps.GetN(); i++) {
          Ptr<IbvInterface> ibvIf = DynamicCast<IbvInterface>(apps.Get(i));
          ibvIf->EnablePollPrint();
        }
        // Let all IbvQPs print their QP State   
        for(uint32_t i = 0; i < apps.GetN(); i++) {
          Ptr<IbvInterface> ibvIf = DynamicCast<IbvInterface>(apps.Get(i));
          std::vector<ns3::Ptr<ns3::IbvQP>> activeQps = ibvIf->GetActiveIbvQps();
          for(uint32_t j = 0; j < activeQps.size(); j++) {
            if (activeQps[j]->GetRdmaQueuePair())
              activeQps[j]->PrintQpState();
          }
        }
      }
    }
  }
  Simulator::Schedule(NanoSeconds(200), PollTimeQuery, timer_sock);
}
void InitializeTimerInterface(int num_nodes) {
  std::vector<int> listen_sock_vec(num_nodes);
  std::vector<int> server_sock_vec(num_nodes);
  for(int i=0;i<num_nodes;i++) {
    listen_sock_vec[i] = InitializeListeningSocket(i); 
  }
  for(int i=0;i<num_nodes;i++) {
    server_sock_vec[i] = AcceptConnection(listen_sock_vec[i]);
    printf("Node %d sock %d\n",i,server_sock_vec[i]);
  }
  timerSocketsVec = server_sock_vec;
  for(int i=0;i<num_nodes;i++) {
    Simulator::ScheduleNow(PollTimeQuery, server_sock_vec[i]);
  }
}

// std::vector<int> InitializeSocketWithDaemonProcess(int num_dockers) {
//   std::vector<int> listen_sock_vec(num_dockers);
//   std::vector<int> server_sock_vec(num_dockers);
//   for(int i=0;i<num_dockers;i++) {
//     listen_sock_vec[i] = InitializeListeningSocket(i); 
//   }
//   for(int i=0;i<num_dockers;i++) {
//     server_sock_vec[i] = AcceptConnection(listen_sock_vec[i]);
//     printf("Docker %d sock %d\n",i,server_sock_vec[i]);
//   }
//   return server_sock_vec;
// }
}