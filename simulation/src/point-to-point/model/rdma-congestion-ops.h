#ifndef RDMA_CONGESTION_OPS_H
#define RDMA_CONGESTION_OPS_H
#include "ns3/timer.h"
#include "rdma-hw.h"
#include "rdma-queue-pair.h"

namespace ns3 {
class RdmaQueuePair;
class RdmaHw;
/******************************
 * RDMA Congestion Ops, an abstract class
 *****************************/
class RdmaCongestionOps : public Object {
 public:
  static TypeId GetTypeId(void);
  void SetHw(Ptr<RdmaHw> r, Ptr<QbbNetDevice> n) {
    rdmaHw = r;
    nic = n;
  }
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate){
    m_ccRate = lazyInitRate;
  }
  /**
   * \brief This function is called when rdma-hw sends a packet.
   */
  virtual void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p);
  /**
   * \brief This function is called when rdma-hw receives an ack,
   * where we suppose that every congestion ops should do something.
   */
  virtual void HandleAck(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch) = 0;
  /**
   * \brief This function is called when a qp is complete.
   */
  virtual void QpComplete(Ptr<RdmaQueuePair> qp);

  /**
   * \brief This function is called when all qps controled by this are complete.
   */
  virtual void AllComplete();

  /**
   * \brief This function is called when changing rate.
   * Notice that ccRate will be bound by rdmaHw->m_minRate and nic->GetDataRate().
   * \param new_rate the new rate
   */
  void ChangeRate(DataRate new_rate);

  /**
   * \brief Get the name of the congestion control algorithm. All subclasses
   * must implement this function.
   *
   * \return A string identifying the name
   */
  virtual std::string GetName() const = 0;
  DataRate m_ccRate;

 protected:
  Ptr<RdmaHw> rdmaHw;
  Ptr<QbbNetDevice> nic;
};
/******************************
 * Mellanox's version of DCQCN (perQP)
 *****************************/
class MellanoxDcqcn : public RdmaCongestionOps {
 public:
  MellanoxDcqcn();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  virtual void AllComplete() override;
  std::string GetName() const override {
    return "MellanoxDCQCN";
  }

  virtual void UpdateAlpha(Ptr<RdmaQueuePair> q);
  virtual void ScheduleUpdateAlpha(Ptr<RdmaQueuePair> q);

  // Mellanox's version of rate decrease
  // It checks every m_rateDecreaseInterval if CNP arrived
  // (m_decrease_cnp_arrived). If so, decrease rate, and reset all rate increase
  // related things
  virtual void CheckRateDecrease(Ptr<RdmaQueuePair> q);
  virtual void ScheduleDecreaseRate(Ptr<RdmaQueuePair> q, uint32_t delta);

  // Mellanox's version of rate increase
  virtual void RateIncEventTimer(Ptr<RdmaQueuePair> q);
  virtual void RateIncEvent(Ptr<RdmaQueuePair> q);
  virtual void FastRecovery(Ptr<RdmaQueuePair> q);
  virtual void ActiveIncrease(Ptr<RdmaQueuePair> q);
  virtual void HyperIncrease(Ptr<RdmaQueuePair> q);

  /* parameters */
  double m_g; // feedback weight
  double m_rateOnFirstCNP; // the fraction of line rate to set on first CNP
  bool m_EcnClampTgtRate;
  double m_rpgTimeReset;
  double m_rateDecreaseInterval;
  uint32_t m_rpgThreshold;
  double m_alpha_resume_interval;

  /* variables */
  DataRate m_targetRate; //< Target rate
  EventId m_eventUpdateAlpha;
  double m_alpha;
  bool m_alpha_cnp_arrived; // indicate if CNP arrived in the last slot
  bool m_first_cnp; // indicate if the current CNP is the first CNP
  EventId m_eventDecreaseRate;
  bool m_decrease_cnp_arrived; // indicate if CNP arrived in the last slot
  uint32_t m_rpTimeStage;
  EventId m_rpTimer;
};

/******************************
 * High Precision CC
 *****************************/
class Hpcc : public RdmaCongestionOps {
 public:
  Hpcc();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "HPCC";
  }

  virtual void UpdateRate(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch,
      bool fast_react);
  virtual void FastReact(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch);

  /* parameters */
  bool m_fast_react;
  double m_targetUtil;
  double m_utilHigh;
  uint32_t m_miThresh;
  bool m_multipleRate;
  bool m_sampleFeedback; // only react to feedback every RTT, or qlen > 0

  /* variables */
  uint64_t m_lastUpdateSeq;
  DataRate m_curRate;
  IntHop hop[IntHeader::maxHop];
  uint32_t keep[IntHeader::maxHop];
  uint32_t m_incStage;
  double m_lastGap;
  double u;
  struct {
    double u;
    DataRate Rc;
    uint32_t incStage;
  } hopState[IntHeader::maxHop];
};

/**********************
 * TIMELY
 *********************/
class Timely : public RdmaCongestionOps {
 public:
  Timely();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "TIMELY";
  }

  virtual void UpdateRate(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch,
      bool us);
  virtual void FastReact(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch);

  /* parameters */
  double m_tmly_alpha, m_tmly_beta;
  uint64_t m_tmly_TLow, m_tmly_THigh, m_tmly_minRtt;

  /* variables */
  uint64_t m_lastUpdateSeq;
  DataRate m_curRate;
  uint32_t m_incStage;
  uint64_t m_lastRtt;
  double m_rttDiff;
};

/**********************
 * DCTCP
 *********************/
class Dctcp : public RdmaCongestionOps {
 public:
  Dctcp();
  static TypeId GetTypeId(void);
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "DCTCP";
  }

  /* parameters */
  DataRate m_dctcp_rai;
  double m_g; // feedback weight

  /* variables */
  uint64_t m_lastUpdateSeq;
  uint32_t m_caState;
  uint64_t m_highSeq; // when to exit cwr
  double m_alpha;
  uint32_t m_ecnCnt;
  uint32_t m_batchSizeOfAlpha;
};

/*********************
 * HPCC-PINT
 ********************/
class HpccPint : public Hpcc {
 public:
  HpccPint();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "HPCC-PINT";
  }

  virtual void UpdateRate(
      Ptr<RdmaQueuePair> qp,
      Ptr<Packet> p,
      CustomHeader& ch,
      bool fast_react) override;

  /* parameters */
  uint32_t m_pint_smpl_thresh;
  double m_pint_prob;
};

/*********************
 * Swift
 ********************/
class Swift : public RdmaCongestionOps {
 public:
  Swift();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  virtual void AllComplete() override;
  
  std::string GetName() const override {
    return "Swift";
  }

  inline void SetCanDecreaseToTrue()
  {
      m_canDecrease = true;
  }

  /* parameters */
  uint64_t m_baseTarget; //!< Base target delay in ns. Default to 25us.
  double m_mdFactor; //!< β in paper. Multiplicative Decrement factor
  double m_maxMdFactor; //!< max_mdf in paper. Maximum Multiplicative Decrement
                        //!< factor
  double m_range; //!< fs_range in paper. Default to 4.0.

  /* variables */
  DataRate m_curRate;

  bool m_canDecrease; //!< Whether the sender can decrease the rate.
  EventId m_eventSetCanDecrease;

  double m_gamma; //!< Used in GetTargetDelay() to simplify the formula.
};

/**********************
 * DCQCN based on real deployment
 *********************/
class RealDcqcn : public RdmaCongestionOps {
 public:
  RealDcqcn();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "RealDcqcn";
  }

  void RampUp();

  /* parameters */
  double m_g; // feedback weight

  Time m_alphaUpdateDelay;
  Time m_rateUpdateDelay;

  uint32_t m_bytesThreshold;
  uint32_t m_F;

  bool m_clampTargetRate;
  bool m_clampTargetRateAfterTimeInc;

  /* variables */
  DataRate m_targetRate; //< Target rate
  DataRate m_maxRate; //< Max rate, assuming it to be the lazyInitRate

  // EventId m_eventUpdateAlpha;
  bool m_first_cnp; // indicate if the current CNP is the first CNP
  Time m_lastUpdateAlphaTime;
  Time m_lastUpdateRateTime;
  // EventId m_eventUpdateRate;
  uint32_t m_bytesCounter;
  uint32_t m_rateUpdateIter;
  uint32_t m_bytesUpdateIter;

  double m_alpha;
};

/**********************
 * HPCC-ECN
 *********************/
class HpccEcn : public RdmaCongestionOps {
 public:
  HpccEcn();
  static TypeId GetTypeId(void);
  virtual void LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) override;
  virtual void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p) override;
  virtual void HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch)
      override;
  std::string GetName() const override {
    return "HPCC-ECN";
  }
  virtual void RampUp();
  virtual void DoInit();
  /* parameters */
  uint32_t m_decreaseShift;
  Time m_rampUpTimer;
  uint32_t m_fastRecoveryThreshold;
  uint32_t m_aiStageThresold;
  Time m_resetTimer;
  
  /* variables */
  DataRate m_lastGoodRate;
  Time m_rampUpTimeStamp;
  uint32_t m_cnpCounter;
  uint32_t m_rampUpStage;
  uint32_t m_fastRecoveryStage;
  DataRate m_maxRate;
};

} // namespace ns3

#endif // RDMA_CONGESTION_OPS_H