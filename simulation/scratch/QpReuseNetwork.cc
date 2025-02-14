#include <execinfo.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <random>
#include "common.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#endif
#ifdef NS3_MPI
#include <mpi.h>
#include "ns3/mpi-interface.h"
#endif

using namespace std;
using namespace ns3;

const bool SPLIT_DATA_ON_QPS = true;
const int QPS_PER_CONNECTION = 2;

extern uint32_t node_num, switch_num, link_num, trace_num, nvswitch_num,
    gpus_per_server;

extern std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t>>
    portNumber;

extern std::ifstream flowf;
extern FlowInput flow_input;

uint32_t msg_num_finished = 0;
uint32_t total_msg_num = 0;
std::unordered_map<std::string, ApplicationContainer> apps;

inline std::string getHashKey(uint32_t src, uint32_t dst, uint32_t pg, uint32_t dport){
    return std::to_string(src) + '_' + std::to_string(dst) + '_' + std::to_string(pg) + '_' + std::to_string(dport);
}

std::vector<Ptr<RdmaClient>> getClients(uint32_t src, uint32_t dst, uint32_t pg, uint32_t dport) {
  std::vector<Ptr<RdmaClient>> clients;
  std::string hashKey = getHashKey(src, dst, pg, dport);  
  #ifdef NS3_MTP
    MtpInterface::explicitCriticalSection cs;
  #endif
  if (apps[hashKey].GetN() == 0) {
    // create apps
    for (int i = 0; i < QPS_PER_CONNECTION; i++) {
      uint32_t port =
          portNumber[flow_input.src][flow_input.dst]++; // get a new port number
      RdmaClientHelper clientHelper(
          pg,
          serverAddress[src],
          serverAddress[dst],
          port,
          dport,
          0, // create a qp w/o message
          has_win ? (global_t == 1 ? maxBdp : pairBdp[n.Get(src)][n.Get(dst)]) : 0,
          global_t == 1 ? maxRtt : pairRtt[src][dst],
          nullptr,
          nullptr,
          1,
          src,
          dst,
          false);
      apps[hashKey].Add(clientHelper.Install(n.Get(src)));
    //   std::cout<<Simulator::Now().GetTimeStep()<<" " <<hashKey<<" add a client at node "<<src<<endl;
    }
    apps[hashKey].Start(Time(0));
  }
  for (int i = 0; i < QPS_PER_CONNECTION; i++) {
    Ptr<RdmaClient> qp = DynamicCast<RdmaClient>(apps[hashKey].Get(i));
    clients.push_back(qp);
  }
  #ifdef NS3_MTP
    cs.ExitSection();
  #endif
  return clients;
}

void PushMessagetoClient(Ptr<RdmaClient> client, uint64_t size) {
  client->PushMessagetoQp(size);
}

void ReadFlowInput() {
  if (flow_input.idx < flow_num) {
    flowf >> flow_input.src >> flow_input.dst >> flow_input.pg >>
        flow_input.dport >> flow_input.maxPacketCount >> flow_input.start_time;
    NS_ASSERT(
        n.Get(flow_input.src)->GetNodeType() == 0 &&
        n.Get(flow_input.dst)->GetNodeType() == 0);
  }
}

void ScheduleFlowInputs() {
  while (flow_input.idx < flow_num && Seconds(flow_input.start_time) == Simulator::Now()) {
    //FlowInput 
    //src, dst, pg, dport;
    //maxPacketCount;
    std::vector<Ptr<RdmaClient>> clients = getClients(flow_input.src, flow_input.dst, flow_input.pg, flow_input.dport);

    if (SPLIT_DATA_ON_QPS == false) {
      // choose a random qp
      int qp_index = std::rand() % QPS_PER_CONNECTION;
      std::cout
          << "注册流" << flow_input.idx << " src: " << flow_input.src
          << " dst: " << flow_input.dst << " pg: " << flow_input.pg
          << " sport: " << clients[qp_index]->GetSourcePort()
          << " dport: " << flow_input.dport << " maxPacketCount: "
          << flow_input.maxPacketCount // 虽然叫maxPacketCount，但是其实是字节数
          << " qp_index: " << qp_index
          << " start_time: " << flow_input.start_time << std::endl;
      total_msg_num++;
      Simulator::Schedule(NanoSeconds(1), PushMessagetoClient, clients[qp_index], flow_input.maxPacketCount);
    } else if (SPLIT_DATA_ON_QPS == true) {
      uint64_t base_size = flow_input.maxPacketCount / QPS_PER_CONNECTION;
      uint64_t last_size = base_size + flow_input.maxPacketCount % QPS_PER_CONNECTION;
      for (int i = 0; i < QPS_PER_CONNECTION; i++) {
        uint64_t size = (i == QPS_PER_CONNECTION - 1)? last_size : base_size;
        total_msg_num++;
        Simulator::Schedule(NanoSeconds(1), PushMessagetoClient, clients[i], size);
      }
    }
    // get the next flow input
    flow_input.idx++;
    ReadFlowInput();
  }

  // schedule the next time to run this function
  if (flow_input.idx < flow_num) {
    Simulator::Schedule(
        Seconds(flow_input.start_time) - Simulator::Now(), ScheduleFlowInputs);
  } else { // no more flows, close the file
    flowf.close();
  }
}

void Finish(){
    for(auto it:apps){
        for(int i = 0;i < it.second.GetN(); i++){
            Ptr<RdmaClient> app = DynamicCast<RdmaClient>(it.second.Get(i));
            app->FinishQp();
        }
    }
}

void qp_finish_reuse(FILE* fout, Ptr<RdmaQueuePair> q) {
  uint32_t sid = ip_to_node_id(q->sip), did = ip_to_node_id(q->dip);
  #ifdef NS3_MTP
  MtpInterface::explicitCriticalSection cs;
  #endif
  Ptr<Node> dstNode = n.Get(did);
  Ptr<RdmaDriver> rdma = dstNode->GetObject<RdmaDriver>();
  rdma->m_rdma->DeleteRxQp(q->sip.Get(), q->m_pg, q->sport);
  std::cout << "at "<< Simulator::Now().GetNanoSeconds()<<"ns, qp finish, src: " << sid << " did: " << did
            << " port: " << q->sport << std::endl;
  #ifdef NS3_MTP
  cs.ExitSection();
  #endif
}

void send_finish_reuse(FILE* fout, Ptr<RdmaQueuePair> q) {
  // Currently do nothing
  // uint32_t sid = ip_to_node_id(q->sip), did = ip_to_node_id(q->dip);
}

void message_finish_reuse(FILE* fout, Ptr<RdmaQueuePair> q, uint64_t msgSize){
  uint32_t sid = ip_to_node_id(q->sip), did = ip_to_node_id(q->dip);
  uint64_t base_rtt = pairRtt[sid][did], b = pairBw[sid][did];
  uint32_t packet_payload_size =
      get_config_value_ns3<uint64_t>("ns3::RdmaHw::Mtu");
  uint64_t size = msgSize;
  uint32_t total_bytes = size +
      ((size - 1) / packet_payload_size + 1) *
          (CustomHeader::GetStaticWholeHeaderSize() -
           IntHeader::GetStaticSize()); // translate to the minimum bytes
                                        // required (with header but no INT)
  uint64_t standalone_fct = base_rtt + total_bytes * 8000000000lu / b;
  fprintf(
      fout,
      "%08x %08x %u %u %lu %lu %lu %lu\n",
      q->sip.Get(),
      q->dip.Get(),
      q->sport,
      q->dport,
      size,
      q->startTime.GetTimeStep(),
      (Simulator::Now() - q->startTime).GetTimeStep(),
      standalone_fct);
  fflush(fout);

  std::cout << "at "<< Simulator::Now().GetNanoSeconds()<<"ns, message finish, src: " << sid << " did: " << did
            << " port: " << q->sport << " total bytes: " << size<< std::endl;
  // Ptr<Node> dstNode = n.Get(did);
  // Ptr<RdmaDriver> rdma = dstNode->GetObject<RdmaDriver>();
  // rdma->m_rdma->DeleteRxQp(q->sip.Get(), q->m_pg, q->sport);
  msg_num_finished++;
  if(msg_num_finished == total_msg_num){
    Finish();
    cancel_monitor();
  }
}

int main(int argc, char* argv[]) {
#ifdef NS3_MTP
  MtpInterface::Enable(16);
#endif

#ifdef NS3_MPI
  ns3::MpiInterface::Enable(&argc, &argv);
// GlobalValue::Bind ("SimulatorImplementationType",
//                    StringValue ("ns3::DistributedSimulatorImpl"));
#endif

  // MPI_Init(&argc, &argv);
  float comm_scale = 1;

  CommandLine cmd;
  cmd.AddValue("commscale", "Communication Scale", comm_scale);
  cmd.Parse(argc, argv);

  clock_t begint, endt;
  begint = clock();

  if (!ReadConf(argc, argv))
    return -1;
  SetConfig();
  SetupNetwork(qp_finish_reuse, send_finish_reuse, message_finish_reuse);

  //
  // Now, do the actual simulation.
  //
  std::cout << "Running Simulation.\n";
  fflush(stdout);
  NS_LOG_INFO("Run Simulation.");

  std::cout << "Flow num: " << flow_num << std::endl;
  flow_input.idx = 0;
  if (flow_num > 0) {
    ReadFlowInput();
    Simulator::Schedule(
        Seconds(flow_input.start_time) - Simulator::Now(), ScheduleFlowInputs);
  }

  Simulator::Run();
  // Simulator::Stop(TimeStep (0x7fffffffffffffffLL));
  Simulator::Stop(Seconds(2000000000));
  Simulator::Destroy();

  endt = clock();
  // std:://cout << (double)(endt - begint) / CLOCKS_PER_SEC << "\n";
  return 0;
}