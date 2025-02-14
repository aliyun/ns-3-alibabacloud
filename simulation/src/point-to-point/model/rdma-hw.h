#ifndef RDMA_HW_H
#define RDMA_HW_H

#include <ns3/custom-header.h>
#include <ns3/node.h>
// #include <ns3/rdma-queue-pair.h>
#include <ns3/rdma.h>
#include <set>
#include <unordered_map>
#include "pint.h"
#include "qbb-net-device.h"
#include "rdma-queue-pair.h"
#include "rdma-congestion-ops.h"

namespace ns3 {
class RdmaQueuePair;
class RdmaQueuePairGroup;
class RdmaRxQueuePair;
class RdmaCongestionOps;

struct RdmaInterfaceMgr {
  // The QbbDevice dev contains many qps in qpGrp
  Ptr<QbbNetDevice> dev;
  Ptr<RdmaQueuePairGroup> qpGrp;

  RdmaInterfaceMgr() : dev(NULL), qpGrp(NULL) {}
  RdmaInterfaceMgr(Ptr<QbbNetDevice> _dev) {
    dev = _dev;
  }
};

class RdmaHw : public Object {
 public:
  static TypeId GetTypeId(void);
  RdmaHw();

  Ptr<Node> m_node;
  uint32_t m_mtu;
  double m_nack_interval;
  uint32_t m_chunk;
  uint32_t m_ack_interval;
  bool m_backto0;
  bool m_var_win; // m_fast_react;
  bool m_rateBound;
  uint32_t m_total_pause_times;
  uint32_t m_paused_times;
  std::vector<RdmaInterfaceMgr>
      m_nic; // list of running nic controlled by this RdmaHw
  std::unordered_map<uint64_t, Ptr<RdmaQueuePair>>
      m_qpMap; // mapping from uint64_t to qp
  std::unordered_map<uint64_t, Ptr<RdmaRxQueuePair>>
      m_rxQpMap; // mapping from uint64_t to rx qp
  std::unordered_map<uint32_t, std::vector<int>>
      m_rtTable; // map from ip address (u32) to possible ECMP port (index of
                 // dev)
  std::unordered_map<uint32_t, std::vector<int>>
      m_rtTable_nxthop_nvswitch; // map from ip address (u32) to possible ECMP
                                 // port (index of dev) connected to nvswitch
  uint32_t m_gpus_per_server; // uesed for routing; if src and dst in the same
                              // server, then communicate by nvswitch.
  uint32_t nvls_enable;
  std::set<uint32_t> nvswitch_set;

  // when qp can choose more than one nics, using round-robin to make qp distribute on each nic fairly
  uint32_t m_lastRouteIdx;
  std::map<Ptr<RdmaQueuePair>, uint32_t> m_qp2devIdx;

  // qp complete callback
  typedef Callback<void, Ptr<RdmaQueuePair>> QpCompleteCallback;
  QpCompleteCallback m_qpCompleteCallback;
  typedef Callback<void, Ptr<RdmaQueuePair>> SendCompleteCallback;
  SendCompleteCallback m_sendCompleteCallback;

  typedef Callback<void, Ptr<RdmaQueuePair>, uint64_t> MessageCompleteCallback;
  MessageCompleteCallback m_messageCompleteCallback; //the first message of the qp complete callback

  // for monitor
  std::vector<uint64_t> tx_bytes; // <port_id, tx_bytes>
  std::unordered_map<uint64_t, uint32_t>
      qp_cnp; // key of qp ---> received cnp number
  std::vector<uint64_t>
      last_tx_bytes; // last sampling value <port_id, tx_bytes>
  std::unordered_map<uint64_t, uint32_t>
      last_qp_cnp; // last sampling value key of qp ---> received cnp number
  std::unordered_map<uint64_t, uint64_t>
      last_qp_rate; // last sampling value key of qp ---> sending rate
  std::vector<uint64_t> rx_bytes; // <port_id, rx_bytes>
  std::vector<uint64_t> last_rx_bytes; // last sampling value
  void UpdateTxBytes(uint32_t port_id, uint64_t bytes);
  void UpdateRxBytes(uint32_t port_id, uint64_t bytes);
  void PrintHostTxBW(FILE* bw_output, uint32_t bw_mon_interval);
  void PrintHostRxBW(FILE* bw_output, uint32_t bw_mon_interval);
  void PrintQPRate(FILE* rate_output);
  void PrintQPCnpNumber(FILE* cnp_output);

  // nvls
  void enable_nvls();
  void disable_nvls();
  void add_nvswitch(uint32_t nvswitch_id);

  void SetNode(Ptr<Node> node);
  void Setup(
      QpCompleteCallback cb,
      SendCompleteCallback send_cb,
      MessageCompleteCallback message_cb); // setup shared data and callbacks with the QbbNetDevice
  static uint64_t GetQpKey(
      uint32_t dip,
      uint16_t sport,
      uint16_t pg); // get the lookup key for m_qpMap
  Ptr<RdmaQueuePair> GetQp(
      uint32_t dip,
      uint16_t sport,
      uint16_t pg); // get the qp
  uint32_t GetNicIdxOfQp(Ptr<RdmaQueuePair> qp); // get the NIC index of the qp
  Ptr<QbbNetDevice> GetNicOfQp(Ptr<RdmaQueuePair> qp) {
    return m_nic[GetNicIdxOfQp(qp)].dev;
  }
  Ptr<RdmaQueuePair> AddQueuePair(
      uint32_t src,
      uint32_t dest,
      uint64_t tag,
      uint64_t size,
      uint16_t pg,
      Ipv4Address _sip,
      Ipv4Address _dip,
      uint16_t _sport,
      uint16_t _dport,
      uint32_t win,
      uint64_t baseRtt,
      Callback<void> notifyAppFinish,
      Callback<void> notifyAppSent); // add a new qp (new send)
  void DeleteQueuePair(Ptr<RdmaQueuePair> qp);

  Ptr<RdmaRxQueuePair> GetRxQp(
      uint32_t sip,
      uint32_t dip,
      uint16_t sport,
      uint16_t dport,
      uint16_t pg,
      bool create); // get a rxQp
  uint32_t GetNicIdxOfRxQp(
      Ptr<RdmaRxQueuePair> q); // get the NIC index of the rxQp
  void DeleteRxQp(uint32_t dip, uint16_t pg, uint16_t dport);

  int ReceiveUdp(Ptr<Packet> p, CustomHeader& ch, uint32_t devIdx);
  int ReceiveCnp(Ptr<Packet> p, CustomHeader& ch, uint32_t devIdx);
  int ReceiveAck(Ptr<Packet> p, CustomHeader& ch, uint32_t devIdx); // handle both ACK and NACK
  int Receive(
      Ptr<Packet> p,
      CustomHeader& ch, uint32_t devIdx); // callback function that the QbbNetDevice should use
                         // when receive packets. Only NIC can call this
                         // function. And do not call this upon PFC

  void PCIePause(uint32_t nic_idx, uint32_t qIndex);
  void PCIeResume(uint32_t nic_idx, uint32_t qIndex);
  void EnablePause();
  bool enable_pcie_pause;

  void CheckandSendQCN(Ptr<RdmaRxQueuePair> q);
  int ReceiverCheckSeq(uint64_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size);
  void AddHeader(Ptr<Packet> p, uint16_t protocolNumber);
  static uint16_t EtherToPpp(uint16_t protocol);

  void RecoverQueue(Ptr<RdmaQueuePair> qp);
  void QpComplete(Ptr<RdmaQueuePair> qp);
  void QpCompleteMessage(Ptr<RdmaQueuePair> qp);
  void SetLinkDown(Ptr<QbbNetDevice> dev);

  int SendPacketComplete(Ptr<Packet> p, CustomHeader& ch);
  void SendComplete(Ptr<RdmaQueuePair> qp);

  // call this function after the NIC is setup
  void AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx, bool is_nvswitch);
  void ClearTable();
  void RedistributeQp();

  Ptr<Packet> GetNxtPacket(
      Ptr<RdmaQueuePair> qp); // get next packet to send, inc snd_nxt
  Ptr<Packet> GenDataPacket(Ptr<RdmaQueuePair> qp, uint32_t pkt_size);
  void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap);
  void UpdateNextAvail(
      Ptr<RdmaQueuePair> qp,
      Time interframeGap,
      uint32_t pkt_size);
  void ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate);
  /******************************
   * Congestion control relevant
   *****************************/
  DataRate m_rai; //< Rate of additive increase
  DataRate m_rhai; //< Rate of hyper-additive increase

  DataRate m_minRate; //< Min sending rate
  uint32_t m_cc_mode;
  using ConfigEntry = std::pair<std::string, Ptr<AttributeValue>>;
  std::vector<ConfigEntry> m_cc_configs; //< Specific congestion control config for this node

  enum NicCoalesceMethod { PER_IP, PER_QP };

  class NicPerIpTableEntry : public Object {
   public:
    static TypeId GetTypeId(void){
        static TypeId tid = TypeId("ns3::RdmaHw::NicPerIpTableEntry")
          .SetParent<Object>();
        return tid;
    }
    NicPerIpTableEntry(){
        qpNum = 0;
    }
    Ptr<RdmaCongestionOps> m_congestionControl;
    uint32_t qpNum;
  };
  NicCoalesceMethod m_nic_coalesce_method;
  std::vector<std::unordered_map<uint32_t, Ptr<NicPerIpTableEntry>>>
      m_nic_peripTable;
  std::unordered_map<uint64_t, Time> m_cnpTable; // key of qp (perQP) | key of nicIdx of qp (perIP) ---> last time send cnp

  Time m_cnp_interval; //< cnp interval
};

} /* namespace ns3 */

#endif /* RDMA_HW_H */
