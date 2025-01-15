#ifndef RDMA_QUEUE_PAIR_H
#define RDMA_QUEUE_PAIR_H

#include <ns3/custom-header.h>
#include <ns3/data-rate.h>
#include <ns3/event-id.h>
#include <ns3/int-header.h>
#include <ns3/ipv4-address.h>
#include <ns3/object.h>
#include <ns3/packet.h>
#include <queue>
#include <vector>
#include <math.h>
#include "rdma-congestion-ops.h"

namespace ns3 {
class RdmaCongestionOps;

class RdmaQueuePair : public Object {
 public:
  Time startTime;
  Ipv4Address sip, dip;
  uint16_t sport, dport;
  uint64_t m_tag;
  uint32_t m_src, m_dest;
  uint64_t snd_nxt, snd_una; // next seq to send, the highest unacked seq
  uint16_t m_pg;
  uint16_t m_ipid;
  uint32_t m_win; // bound of on-the-fly packets
  uint64_t m_baseRtt; // base RTT of this qp
  DataRate m_max_rate; // max rate
  bool m_var_win; // variable window size
  Time m_nextAvail; //< Soonest time of next send
  uint32_t wp; // current window of packets
  uint32_t lastPktSize;
  // Callback<void> m_notifyAppFinish;
  // Callback<void> m_notifyAppSent;
  /******************************
   * runtime states
   *****************************/
  uint32_t nvls_enable;
  DataRate m_rate; //< Current rate
  Ptr<RdmaCongestionOps> m_congestionControl;

  /***********
   * methods
   **********/
  static TypeId GetTypeId(void);
  RdmaQueuePair(
      uint16_t pg,
      Ipv4Address _sip,
      Ipv4Address _dip,
      uint16_t _sport,
      uint16_t _dport);
  void SetWin(uint32_t win);
  void SetBaseRtt(uint64_t baseRtt);
  void SetVarWin(bool v);

  uint64_t GetBytesLeft();
  uint32_t GetSrc();
  uint32_t GetDest();
  uint64_t GetTag();
  void SetTag(uint64_t tag);
  void SetSrc(uint32_t src);
  void SetDest(uint32_t dest);
  uint32_t GetHash(void);
  void Acknowledge(uint64_t ack);
  uint64_t GetOnTheFly();
  bool IsWinBound();
  uint64_t GetWin(); // window size calculated from m_rate
  /**
   * \brief Check if there is no more message to send
   */
  bool IsFinished();
  // uint64_t HpGetCurWin(); // window size calculated from hp.m_curRate, used by HPCC

  void UpdateRate();

  /***********
   * tokens
   **********/
  class TokenBucket {
   public:
    int64_t m_tokens; // bytes
    int64_t m_tokensMax; // bytes
    Time m_lastTokenStamp;
    void Update(Time now, DataRate rate) {
      if (m_tokens < m_tokensMax) {
        int64_t delta = std::round((now - m_lastTokenStamp).GetSeconds() *
            rate.GetBitRate() / 8); // bytes
        m_tokens = std::min(m_tokens + delta, m_tokensMax);
        m_lastTokenStamp = now;
      }
    }
    void Init(int64_t initToken, Time now, int64_t tokensMax){
      m_tokens = initToken;
      m_lastTokenStamp = now;
      m_tokensMax = tokensMax;
    }
    bool hasEnoughTokens(uint32_t bytes) {
      return m_tokens >= bytes;
    }
    void consumeTokens(uint32_t bytes) {
      m_tokens -= bytes; // token could be negative
      // this bucket will consume tokens only when hasEnoughTokens(min(TOKEN_PER_ROUND, lastPacketSize))
    }
  };
  TokenBucket m_tokenBucket;
  
  /***********
   * messages
   **********/
  class RdmaMessage {
   public:
    uint64_t m_size;
    uint64_t m_startSeq;
    Callback<void> m_notifyAppFinish;
    Callback<void> m_notifyAppSent;
  };
  std::queue<RdmaMessage> m_messages;
  void PushMessage(uint64_t size, Callback<void> notifyAppFinish, Callback<void> notifyAppSent);
  void FinishMessage();

  bool IsCurMessageFinished();
};

class RdmaRxQueuePair : public Object { // Rx side queue pair
 public:
  struct ECNAccount {
    uint16_t qIndex;
    uint8_t ecnbits;
    uint16_t qfb;
    uint16_t total;

    ECNAccount():qIndex(0), ecnbits(0), qfb(0), total(0) {}
  };
  ECNAccount m_ecn_source;
  uint32_t sip, dip;
  uint16_t sport, dport;
  uint16_t m_ipid;
  uint64_t ReceiverNextExpectedSeq;
  Time m_nackTimer;
  int32_t m_milestone_rx;
  uint32_t m_lastNACK;
  EventId QcnTimerEvent; // if destroy this rxQp, remember to cancel this timer

  static TypeId GetTypeId(void);
  RdmaRxQueuePair();
  uint32_t GetHash(void);
};

class RdmaQueuePairGroup : public Object {
 public:
  std::vector<Ptr<RdmaQueuePair>> m_qps;
  // std::vector<Ptr<RdmaRxQueuePair> > m_rxQps;

  static TypeId GetTypeId(void);
  RdmaQueuePairGroup(void);
  uint32_t GetN(void);
  Ptr<RdmaQueuePair> Get(uint32_t idx);
  Ptr<RdmaQueuePair> operator[](uint32_t idx);
  void AddQp(Ptr<RdmaQueuePair> qp);
  // void AddRxQp(Ptr<RdmaRxQueuePair> rxQp);
  /**
   * \warning This function itself is not thread safe. Using it within explicitCriticalSection.
   * \param rr_last the last rr index. It will be updated to the correct position after removing.
   */
  void RemoveFinishedQps(uint32_t &rr_last);

  void Clear(void);
};

} // namespace ns3

#endif /* RDMA_QUEUE_PAIR_H */
