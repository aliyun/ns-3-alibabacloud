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
#include "timer-interface.h"

#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#endif
#ifdef NS3_MPI
#include <mpi.h>
#include "ns3/mpi-interface.h"
#endif

using namespace std;
using namespace ns3;

extern uint32_t node_num, switch_num, link_num, trace_num, nvswitch_num,
    gpus_per_server;

extern std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t>>
    portNumber;

extern std::ifstream flowf;
extern FlowInput flow_input;

uint32_t flow_num_finished = 0;

#define MAX_QPS 2
#define SHM_SEGMENT_SIZE 1 << 28
inline std::string getHashKey(uint32_t src, uint32_t dst, uint32_t pg, uint32_t dport){
    return std::to_string(src) + '_' + std::to_string(dst) + '_' + std::to_string(pg) + '_' + std::to_string(dport);
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

ApplicationContainer InstallRdmaApps(NodeContainer &n, int num_nodes) {
  ApplicationContainer apps;
  for(uint32_t i = 0; i < n.GetN() && i < num_nodes; i++) {
    if(n.Get(i)->GetNodeType() == 0) {
      IbvInterfaceHelper interfaceHelper((SHM_NAME_PREFIX+std::to_string(i)).c_str(), SHM_SEGMENT_SIZE);
      apps.Add(interfaceHelper.Install(n.Get(i)));
    } 
  }
  return apps;
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

  // std::cout << "at "<< Simulator::Now().GetNanoSeconds()<<"ns, message finish, src: " << sid << " did: " << did
  //           << " port: " << q->sport << " total bytes: " << size<< std::endl;
  // Ptr<Node> dstNode = n.Get(did);
  // Ptr<RdmaDriver> rdma = dstNode->GetObject<RdmaDriver>();
  // rdma->m_rdma->DeleteRxQp(q->sip.Get(), q->m_pg, q->sport);
  flow_num_finished++;
  if(flow_num_finished == flow_num){
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
  bool enable_p2p = true;
  int num_nodes = 2;
  LogComponentEnable("IbvInterface", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
  LogComponentEnable("IbvQP", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
  // LogComponentEnable("IbvCQ", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
  LogComponentEnable("RdmaQueuePair", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
  LogComponentEnable("RdmaHw", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));
  LogComponentEnable("QbbNetDevice", (LogLevel)(LOG_LOGIC | LOG_PREFIX_TIME | LOG_PREFIX_LEVEL));

  CommandLine cmd;
  cmd.AddValue("commscale", "Communication Scale", comm_scale);
  cmd.AddValue("numnodes", "Number of Nodes", num_nodes);
  cmd.AddValue("enable_p2p", "Enable P2P", enable_p2p);
  cmd.Parse(argc, argv);

  clock_t begint, endt;
  begint = clock();

  if (!ReadConf(argc, argv))
    return -1;
  SetConfig();
  SetupNetwork(qp_finish_reuse, send_finish_reuse, message_finish_reuse);
  
  // vector<int> docker_sock_vec = InitializeSocketWithDaemonProcess(1);
  InitializeTimerInterface(num_nodes);
  // for(int i = 0; i < num_nodes; i++) {
  //   int docker_id = i / 8;
  //   int docker_sock = docker_sock_vec[docker_id];
  //   IbvInterface::node2daemonsockfd_map[i] = docker_sock;
  // }

  //
  // Now, do the actual simulation.
  //
  std::cout << "Running Simulation.\n";
  fflush(stdout);
  NS_LOG_INFO("Run Simulation.");

  std::cout << "Start to install IbvInterface applications." << std::endl;

  apps = InstallRdmaApps(n, num_nodes);

  for(uint32_t i = 0; i < apps.GetN(); i++) {
    Ptr<IbvInterface> ibvIf = DynamicCast<IbvInterface>(apps.Get(i));
    int node_id = ibvIf->GetNode()->GetId();
    ibvIf->SetRoutingTableIpv4Address(serverAddress[node_id]);
    ibvIf->SetRoutingTableMaskAddress(Ipv4Mask(0xff000000));
    ibvIf->SetGpusPerServer(gpus_per_server);
    if(enable_p2p) {
      ibvIf->EnableGpuP2p();
    } else {
      ibvIf->DisableGpuP2p();
    }
  }
  apps.Start(NanoSeconds(1));
  std::cout << "Succeed to install IbvInterface applications." << std::endl;
  

  Simulator::Run();
  // Simulator::Stop(TimeStep (0x7fffffffffffffffLL));
  Simulator::Stop(Seconds(2000000000));
  Simulator::Destroy();

  endt = clock();
  // std:://cout << (double)(endt - begint) / CLOCKS_PER_SEC << "\n";
  return 0;
}