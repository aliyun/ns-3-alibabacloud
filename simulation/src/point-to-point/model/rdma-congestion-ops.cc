#include "rdma-congestion-ops.h"
#include <ns3/simulator.h>
#include <ns3/udp-header.h>
#include <iostream> // debug
#include <math.h>
#include "cn-header.h"
#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"
#include "qbb-header.h"
namespace ns3 {
NS_LOG_COMPONENT_DEFINE("RdmaCongestionOps");

/******************************
 * RDMA Congestion Ops, an abstract class
 *****************************/
NS_OBJECT_ENSURE_REGISTERED(RdmaCongestionOps);

TypeId RdmaCongestionOps::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::RdmaCongestionOps").SetParent<Object>();
  return tid;
}

void RdmaCongestionOps::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p) {
  // do nothing
}

void RdmaCongestionOps::QpComplete(Ptr<RdmaQueuePair> qp) {
  // do nothing
}

void RdmaCongestionOps::AllComplete() {
  rdmaHw = nullptr;
  nic = nullptr;
}

void RdmaCongestionOps::ChangeRate(DataRate new_rate) {
  DataRate old_rate = m_ccRate;
  m_ccRate = std::min(new_rate, nic->GetDataRate());
  m_ccRate = std::max(m_ccRate, rdmaHw->m_minRate);
  if (old_rate != m_ccRate) {
    nic->TriggerTransmit(); // change rate would trigger transmit
  }
}

/******************************
 * Mellanox's version of DCQCN (perQP)
 *****************************/
NS_OBJECT_ENSURE_REGISTERED(MellanoxDcqcn);

TypeId MellanoxDcqcn::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::MellanoxDcqcn")
          .SetParent<RdmaCongestionOps>()
          .AddConstructor<MellanoxDcqcn>()
          .AddAttribute(
              "EwmaGain",
              "Control gain parameter which determines the level of rate decrease",
              DoubleValue(1.0 / 16),
              MakeDoubleAccessor(&MellanoxDcqcn::m_g),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "RateOnFirstCnp",
              "the fraction of rate on first CNP",
              DoubleValue(1.0),
              MakeDoubleAccessor(&MellanoxDcqcn::m_rateOnFirstCNP),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "ClampTargetRate",
              "Clamp target rate.",
              BooleanValue(false),
              MakeBooleanAccessor(&MellanoxDcqcn::m_EcnClampTgtRate),
              MakeBooleanChecker())
          .AddAttribute(
              "RPTimer",
              "The rate increase timer at RP in microseconds",
              DoubleValue(1500.0),
              MakeDoubleAccessor(&MellanoxDcqcn::m_rpgTimeReset),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "RateDecreaseInterval",
              "The interval of rate decrease check",
              DoubleValue(4.0),
              MakeDoubleAccessor(&MellanoxDcqcn::m_rateDecreaseInterval),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "FastRecoveryTimes",
              "The rate increase timer at RP",
              UintegerValue(5),
              MakeUintegerAccessor(&MellanoxDcqcn::m_rpgThreshold),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "AlphaResumInterval",
              "The interval of resuming alpha",
              DoubleValue(55.0),
              MakeDoubleAccessor(&MellanoxDcqcn::m_alpha_resume_interval),
              MakeDoubleChecker<double>());
  return tid;
}

MellanoxDcqcn::MellanoxDcqcn() {
  // m_targetRate = initRate;
  m_alpha = 1;
  m_alpha_cnp_arrived = false;
  m_first_cnp = true;
  m_decrease_cnp_arrived = false;
  m_rpTimeStage = 0;
}

void MellanoxDcqcn::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_targetRate = lazyInitRate;
}

void MellanoxDcqcn::HandleAck(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch) {
  uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
  if (cnp) {
    m_alpha_cnp_arrived = true; // set CNP_arrived bit for alpha update
    m_decrease_cnp_arrived = true; // set CNP_arrived bit for rate decrease
    if (m_first_cnp) {
      // init alpha
      m_alpha = 1;
      m_alpha_cnp_arrived = false;
      // schedule alpha update
      ScheduleUpdateAlpha(qp);
      // schedule rate decrease
      ScheduleDecreaseRate(
          qp, 1); // add 1 ns to make sure rate decrease is after alpha update
      // set rate on first CNP
      ChangeRate(m_ccRate * m_rateOnFirstCNP);
      m_targetRate = m_rateOnFirstCNP * m_ccRate;
      m_first_cnp = false;
    }
  }
}

void MellanoxDcqcn::AllComplete() {
  Simulator::Cancel(m_eventUpdateAlpha);
  Simulator::Cancel(m_eventDecreaseRate);
  Simulator::Cancel(m_rpTimer);
  RdmaCongestionOps::AllComplete();
}

void MellanoxDcqcn::UpdateAlpha(Ptr<RdmaQueuePair> q) {
#if PRINT_LOG
// printf("%lu alpha update: %08x %08x %u %u %.6lf->",
// Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport,
// q->dport, m_alpha);
#endif
  if (m_alpha_cnp_arrived) {
    m_alpha = (1 - m_g) * m_alpha + m_g; // binary feedback
  } else {
    m_alpha = (1 - m_g) * m_alpha; // binary feedback
  }
#if PRINT_LOG
// printf("%.6lf\n", m_alpha);
#endif
  m_alpha_cnp_arrived = false; // clear the CNP_arrived bit
  ScheduleUpdateAlpha(q);
}

void MellanoxDcqcn::ScheduleUpdateAlpha(Ptr<RdmaQueuePair> q) {
  m_eventUpdateAlpha = Simulator::Schedule(
      MicroSeconds(m_alpha_resume_interval),
      &MellanoxDcqcn::UpdateAlpha,
      this,
      q);
}

void MellanoxDcqcn::CheckRateDecrease(Ptr<RdmaQueuePair> q) {
  ScheduleDecreaseRate(q, 0);
  if (m_decrease_cnp_arrived) {
#if PRINT_LOG
    printf(
        "%lu rate dec: %08x %08x %u %u (%0.3lf %.3lf)->",
        Simulator::Now().GetTimeStep(),
        q->sip.Get(),
        q->dip.Get(),
        q->sport,
        q->dport,
        m_targetRate.GetBitRate() * 1e-9,
        m_ccRate.GetBitRate() * 1e-9);
#endif
    bool clamp = true;
    if (!m_EcnClampTgtRate) {
      if (m_rpTimeStage == 0)
        clamp = false;
    }
    if (clamp)
      m_targetRate = m_ccRate;
    ChangeRate(m_ccRate * (1 - m_alpha / 2));
    // reset rate increase related things
    m_rpTimeStage = 0;
    m_decrease_cnp_arrived = false;
    Simulator::Cancel(m_rpTimer);
    m_rpTimer = Simulator::Schedule(
        MicroSeconds(m_rpgTimeReset),
        &MellanoxDcqcn::RateIncEventTimer,
        this,
        q);
#if PRINT_LOG
    printf(
        "(%.3lf %.3lf)\n",
        m_targetRate.GetBitRate() * 1e-9,
        m_ccRate.GetBitRate() * 1e-9);
#endif
  }
}

void MellanoxDcqcn::ScheduleDecreaseRate(Ptr<RdmaQueuePair> q, uint32_t delta) {
  m_eventDecreaseRate = Simulator::Schedule(
      MicroSeconds(m_rateDecreaseInterval),
      &MellanoxDcqcn::CheckRateDecrease,
      this,
      q);
}

void MellanoxDcqcn::RateIncEventTimer(Ptr<RdmaQueuePair> q) {
  m_rpTimer = Simulator::Schedule(
      MicroSeconds(m_rpgTimeReset), &MellanoxDcqcn::RateIncEventTimer, this, q);
  RateIncEvent(q);
  m_rpTimeStage++;
}

void MellanoxDcqcn::RateIncEvent(Ptr<RdmaQueuePair> q) {
  // check which increase phase: fast recovery, active increase, hyper increase
  if (m_rpTimeStage < m_rpgThreshold) { // fast recovery
    FastRecovery(q);
  } else if (m_rpTimeStage == m_rpgThreshold) { // active increase
    ActiveIncrease(q);
  } else { // hyper increase
    HyperIncrease(q);
  }
}

void MellanoxDcqcn::FastRecovery(Ptr<RdmaQueuePair> q) {
#if PRINT_LOG
  printf(
      "%lu fast recovery: %08x %08x %u %u (%0.3lf %.3lf)->",
      Simulator::Now().GetTimeStep(),
      q->sip.Get(),
      q->dip.Get(),
      q->sport,
      q->dport,
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
  ChangeRate((m_ccRate / 2) + (m_targetRate / 2));
#if PRINT_LOG
  printf(
      "(%.3lf %.3lf)\n",
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
}

void MellanoxDcqcn::ActiveIncrease(Ptr<RdmaQueuePair> q) {
#if PRINT_LOG
  printf(
      "%lu active inc: %08x %08x %u %u (%0.3lf %.3lf)->",
      Simulator::Now().GetTimeStep(),
      q->sip.Get(),
      q->dip.Get(),
      q->sport,
      q->dport,
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
  // get NIC
  Ptr<QbbNetDevice> dev = rdmaHw->GetNicOfQp(q);
  // increate rate
  m_targetRate += rdmaHw->m_rai;
  if (m_targetRate > dev->GetDataRate())
    m_targetRate = dev->GetDataRate();
  ChangeRate((m_ccRate / 2) + (m_targetRate / 2));
#if PRINT_LOG
  printf(
      "(%.3lf %.3lf)\n",
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
}

void MellanoxDcqcn::HyperIncrease(Ptr<RdmaQueuePair> q) {
#if PRINT_LOG
  printf(
      "%lu hyper inc: %08x %08x %u %u (%0.3lf %.3lf)->",
      Simulator::Now().GetTimeStep(),
      q->sip.Get(),
      q->dip.Get(),
      q->sport,
      q->dport,
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
  // get NIC
  Ptr<QbbNetDevice> dev = rdmaHw->GetNicOfQp(q);
  // increate rate
  m_targetRate += rdmaHw->m_rhai;
  if (m_targetRate > dev->GetDataRate())
    m_targetRate = dev->GetDataRate();
  ChangeRate((m_ccRate / 2) + (m_targetRate / 2));
#if PRINT_LOG
  printf(
      "(%.3lf %.3lf)\n",
      m_targetRate.GetBitRate() * 1e-9,
      m_ccRate.GetBitRate() * 1e-9);
#endif
}

/******************************
 * High Precision CC
 *****************************/
NS_OBJECT_ENSURE_REGISTERED(Hpcc);

TypeId Hpcc::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::Hpcc")
          .SetParent<RdmaCongestionOps>()
          .AddConstructor<Hpcc>()
          .AddAttribute(
              "FastReact",
              "Fast React to congestion feedback",
              BooleanValue(true),
              MakeBooleanAccessor(&Hpcc::m_fast_react),
              MakeBooleanChecker())
          .AddAttribute(
              "MiThresh",
              "Threshold of number of consecutive AI before MI",
              UintegerValue(5),
              MakeUintegerAccessor(&Hpcc::m_miThresh),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TargetUtil",
              "The Target Utilization of the bottleneck bandwidth, by default 95%",
              DoubleValue(0.95),
              MakeDoubleAccessor(&Hpcc::m_targetUtil),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "UtilHigh",
              "The upper bound of Target Utilization of the bottleneck bandwidth, by default 98%",
              DoubleValue(0.98),
              MakeDoubleAccessor(&Hpcc::m_utilHigh),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "MultiRate",
              "Maintain multiple rates in HPCC",
              BooleanValue(true),
              MakeBooleanAccessor(&Hpcc::m_multipleRate),
              MakeBooleanChecker())
          .AddAttribute(
              "SampleFeedback",
              "Whether sample feedback or not",
              BooleanValue(false),
              MakeBooleanAccessor(&Hpcc::m_sampleFeedback),
              MakeBooleanChecker());
  return tid;
}

Hpcc::Hpcc() {
  m_lastUpdateSeq = 0;
  for (uint32_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++)
    keep[i] = 0;
  m_incStage = 0;
  m_lastGap = 0;
  u = 1;
  for (uint32_t i = 0; i < IntHeader::maxHop; i++) {
    hopState[i].u = 1;
    hopState[i].incStage = 0;
  }
}

void Hpcc::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_curRate = lazyInitRate;
  if (m_multipleRate) {
    for (uint32_t i = 0; i < IntHeader::maxHop; i++) {
      hopState[i].Rc = lazyInitRate;
    }
  }
}

void Hpcc::HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  uint64_t ack_seq = ch.ack.seq;
  // update rate
  if (ack_seq >
      m_lastUpdateSeq) { // if full RTT feedback is ready, do full update
    UpdateRate(qp, p, ch, false);
  } else { // do fast react
    FastReact(qp, p, ch);
  }
}

void Hpcc::UpdateRate(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch,
    bool fast_react) {
  uint64_t next_seq = qp->snd_nxt;
  bool print = !fast_react || true;
  if (m_lastUpdateSeq == 0) { // first RTT
    m_lastUpdateSeq = next_seq;
    // store INT
    IntHeader& ih = ch.ack.ih;
    NS_ASSERT(ih.nhop <= IntHeader::maxHop);
    for (uint32_t i = 0; i < ih.nhop; i++)
      hop[i] = ih.hop[i];
#if PRINT_LOG
    if (print) {
      printf(
          "%lu %s %08x %08x %u %u [%u,%u,%u]",
          Simulator::Now().GetTimeStep(),
          fast_react ? "fast" : "update",
          qp->sip.Get(),
          qp->dip.Get(),
          qp->sport,
          qp->dport,
          m_lastUpdateSeq,
          ch.ack.seq,
          next_seq);
      for (uint32_t i = 0; i < ih.nhop; i++)
        printf(
            " %u %lu %lu",
            ih.hop[i].GetQlen(),
            ih.hop[i].GetBytes(),
            ih.hop[i].GetTime());
      printf("\n");
    }
#endif
  } else {
    // check packet INT
    IntHeader& ih = ch.ack.ih;
    if (ih.nhop <= IntHeader::maxHop) {
      double max_c = 0;
      bool inStable = false;
#if PRINT_LOG
      if (print)
        printf(
            "%lu %s %08x %08x %u %u [%u,%u,%u]",
            Simulator::Now().GetTimeStep(),
            fast_react ? "fast" : "update",
            qp->sip.Get(),
            qp->dip.Get(),
            qp->sport,
            qp->dport,
            m_lastUpdateSeq,
            ch.ack.seq,
            next_seq);
#endif
      // check each hop
      double U = 0;
      uint64_t dt = 0;
      bool updated[IntHeader::maxHop] = {false}, updated_any = false;
      NS_ASSERT(ih.nhop <= IntHeader::maxHop);
      for (uint32_t i = 0; i < ih.nhop; i++) {
        if (m_sampleFeedback) {
          if (ih.hop[i].GetQlen() == 0 && fast_react)
            continue;
        }
        updated[i] = updated_any = true;
#if PRINT_LOG
        if (print)
          printf(
              " %u(%u) %lu(%lu) %lu(%lu)",
              ih.hop[i].GetQlen(),
              hop[i].GetQlen(),
              ih.hop[i].GetBytes(),
              hop[i].GetBytes(),
              ih.hop[i].GetTime(),
              hop[i].GetTime());
#endif
        uint64_t tau = ih.hop[i].GetTimeDelta(hop[i]);
        ;
        double duration = tau * 1e-9;
        double txRate = (ih.hop[i].GetBytesDelta(hop[i])) * 8 / duration;
        double u = txRate / ih.hop[i].GetLineRate() +
            (double)std::min(ih.hop[i].GetQlen(), hop[i].GetQlen()) *
                qp->m_max_rate.GetBitRate() / ih.hop[i].GetLineRate() /
                qp->m_win;
#if PRINT_LOG
        if (print)
          printf(" %.3lf %.3lf", txRate, u);
#endif
        if (!m_multipleRate) {
          // for aggregate (single R)
          if (u > U) {
            U = u;
            dt = tau;
          }
        } else {
          // for per hop (per hop R)
          if (tau > qp->m_baseRtt)
            tau = qp->m_baseRtt;
          hopState[i].u = (hopState[i].u * (qp->m_baseRtt - tau) + u * tau) /
              double(qp->m_baseRtt);
        }
        hop[i] = ih.hop[i];
      }

      DataRate new_rate;
      int32_t new_incStage;
      DataRate new_rate_per_hop[IntHeader::maxHop];
      int32_t new_incStage_per_hop[IntHeader::maxHop];
      if (!m_multipleRate) {
        // for aggregate (single R)
        if (updated_any) {
          if (dt > qp->m_baseRtt)
            dt = qp->m_baseRtt;
          u = (u * (qp->m_baseRtt - dt) + U * dt) / double(qp->m_baseRtt);
          max_c = u / m_targetUtil;

          if (max_c >= 1 || m_incStage >= m_miThresh) {
            new_rate = m_curRate / max_c + rdmaHw->m_rai;
            new_incStage = 0;
          } else {
            new_rate = m_curRate + rdmaHw->m_rai;
            new_incStage = m_incStage + 1;
          }
          if (new_rate < rdmaHw->m_minRate)
            new_rate = rdmaHw->m_minRate;
          if (new_rate > qp->m_max_rate)
            new_rate = qp->m_max_rate;
#if PRINT_LOG
          if (print)
            printf(" u=%.6lf U=%.3lf dt=%u max_c=%.3lf", u, U, dt, max_c);
#endif
#if PRINT_LOG
          if (print)
            printf(
                " rate:%.3lf->%.3lf\n",
                m_curRate.GetBitRate() * 1e-9,
                new_rate.GetBitRate() * 1e-9);
#endif
        }
      } else {
        // for per hop (per hop R)
        new_rate = qp->m_max_rate;
        for (uint32_t i = 0; i < ih.nhop; i++) {
          if (updated[i]) {
            double c = hopState[i].u / m_targetUtil;
            if (c >= 1 || hopState[i].incStage >= m_miThresh) {
              new_rate_per_hop[i] = hopState[i].Rc / c + rdmaHw->m_rai;
              new_incStage_per_hop[i] = 0;
            } else {
              new_rate_per_hop[i] = hopState[i].Rc + rdmaHw->m_rai;
              new_incStage_per_hop[i] = hopState[i].incStage + 1;
            }
            // bound rate
            if (new_rate_per_hop[i] < rdmaHw->m_minRate)
              new_rate_per_hop[i] = rdmaHw->m_minRate;
            if (new_rate_per_hop[i] > qp->m_max_rate)
              new_rate_per_hop[i] = qp->m_max_rate;
            // find min new_rate
            if (new_rate_per_hop[i] < new_rate)
              new_rate = new_rate_per_hop[i];
#if PRINT_LOG
            if (print)
              printf(" [%u]u=%.6lf c=%.3lf", i, hopState[i].u, c);
#endif
#if PRINT_LOG
            if (print)
              printf(
                  " %.3lf->%.3lf",
                  hopState[i].Rc.GetBitRate() * 1e-9,
                  new_rate.GetBitRate() * 1e-9);
#endif
          } else {
            if (hopState[i].Rc < new_rate)
              new_rate = hopState[i].Rc;
          }
        }
#if PRINT_LOG
        printf("\n");
#endif
      }
      if (updated_any)
        ChangeRate(new_rate);
      if (!fast_react) {
        if (updated_any) {
          m_curRate = new_rate;
          m_incStage = new_incStage;
        }
        if (m_multipleRate) {
          // for per hop (per hop R)
          for (uint32_t i = 0; i < ih.nhop; i++) {
            if (updated[i]) {
              hopState[i].Rc = new_rate_per_hop[i];
              hopState[i].incStage = new_incStage_per_hop[i];
            }
          }
        }
      }
    }
    if (!fast_react) {
      if (next_seq > m_lastUpdateSeq)
        m_lastUpdateSeq = next_seq; //+ rand() % 2 * m_mtu;
    }
  }
}

void Hpcc::FastReact(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  if (m_fast_react)
    UpdateRate(qp, p, ch, true);
}

/**********************
 * TIMELY
 *********************/
NS_OBJECT_ENSURE_REGISTERED(Timely);

TypeId Timely::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::Timely")
                          .SetParent<RdmaCongestionOps>()
                          .AddConstructor<Timely>()
                          .AddAttribute(
                              "TimelyAlpha",
                              "Alpha of TIMELY",
                              DoubleValue(0.875),
                              MakeDoubleAccessor(&Timely::m_tmly_alpha),
                              MakeDoubleChecker<double>())
                          .AddAttribute(
                              "TimelyBeta",
                              "Beta of TIMELY",
                              DoubleValue(0.8),
                              MakeDoubleAccessor(&Timely::m_tmly_beta),
                              MakeDoubleChecker<double>())
                          .AddAttribute(
                              "TimelyTLow",
                              "TLow of TIMELY (ns)",
                              UintegerValue(50000),
                              MakeUintegerAccessor(&Timely::m_tmly_TLow),
                              MakeUintegerChecker<uint64_t>())
                          .AddAttribute(
                              "TimelyTHigh",
                              "THigh of TIMELY (ns)",
                              UintegerValue(500000),
                              MakeUintegerAccessor(&Timely::m_tmly_THigh),
                              MakeUintegerChecker<uint64_t>())
                          .AddAttribute(
                              "TimelyMinRtt",
                              "MinRtt of TIMELY (ns)",
                              UintegerValue(20000),
                              MakeUintegerAccessor(&Timely::m_tmly_minRtt),
                              MakeUintegerChecker<uint64_t>());
  return tid;
}

Timely::Timely() {
  m_lastUpdateSeq = 0;
  m_incStage = 0;
  m_lastRtt = 0;
  m_rttDiff = 0;
}

void Timely::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_curRate = lazyInitRate;
}

void Timely::HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  uint64_t ack_seq = ch.ack.seq;
  // update rate
  if (ack_seq >
      m_lastUpdateSeq) { // if full RTT feedback is ready, do full update
    UpdateRate(qp, p, ch, false);
  } else { // do fast react
    FastReact(qp, p, ch);
  }
}

void Timely::UpdateRate(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch,
    bool us) {
  uint64_t next_seq = qp->snd_nxt;
  uint64_t rtt = Simulator::Now().GetTimeStep() - ch.ack.ih.ts;
  bool print = !us;
  if (m_lastUpdateSeq != 0) { // not first RTT
    int64_t new_rtt_diff = (int64_t)rtt - (int64_t)m_lastRtt;
    double rtt_diff =
        (1 - m_tmly_alpha) * m_rttDiff + m_tmly_alpha * new_rtt_diff;
    double gradient = rtt_diff / m_tmly_minRtt;
    bool inc = false;
    double c = 0;
#if PRINT_LOG
    if (print)
      printf(
          "%lu node:%u rtt:%lu rttDiff:%.0lf gradient:%.3lf rate:%.3lf",
          Simulator::Now().GetTimeStep(),
          m_node->GetId(),
          rtt,
          rtt_diff,
          gradient,
          m_curRate.GetBitRate() * 1e-9);
#endif
    if (rtt < m_tmly_TLow) {
      inc = true;
    } else if (rtt > m_tmly_THigh) {
      c = 1 - m_tmly_beta * (1 - (double)m_tmly_THigh / rtt);
      inc = false;
    } else if (gradient <= 0) {
      inc = true;
    } else {
      c = 1 - m_tmly_beta * gradient;
      if (c < 0)
        c = 0;
      inc = false;
    }
    if (inc) {
      if (m_incStage < 5) {
        ChangeRate(m_curRate + rdmaHw->m_rai);
      } else {
        ChangeRate(m_curRate + rdmaHw->m_rhai);
      }
      if (!us) {
        m_curRate = m_ccRate;
        m_incStage++;
        m_rttDiff = rtt_diff;
      }
    } else {
      ChangeRate(m_curRate * c);
      if (!us) {
        m_curRate = m_ccRate;
        m_incStage = 0;
        m_rttDiff = rtt_diff;
      }
    }
#if PRINT_LOG
    if (print) {
      printf(" %c %.3lf\n", inc ? '^' : 'v', m_ccRate.GetBitRate() * 1e-9);
    }
#endif
  }
  if (!us && next_seq > m_lastUpdateSeq) {
    m_lastUpdateSeq = next_seq;
    // update
    m_lastRtt = rtt;
  }
}

void Timely::FastReact(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  // do nothing
}

/**********************
 * DCTCP
 *********************/
NS_OBJECT_ENSURE_REGISTERED(Dctcp);

TypeId Dctcp::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::Dctcp")
          .SetParent<RdmaCongestionOps>()
          .AddConstructor<Dctcp>()
          .AddAttribute(
              "DctcpRateAI",
              "DCTCP's Rate increment unit in AI period",
              DataRateValue(DataRate("1000Mb/s")),
              MakeDataRateAccessor(&Dctcp::m_dctcp_rai),
              MakeDataRateChecker())
          .AddAttribute(
              "DctcpEwmaGain",
              "Control gain parameter which determines the level of rate decrease",
              DoubleValue(1.0 / 16),
              MakeDoubleAccessor(&Dctcp::m_g),
              MakeDoubleChecker<double>());
  return tid;
}

Dctcp::Dctcp() {
  m_lastUpdateSeq = 0;
  m_caState = 0;
  m_highSeq = 0;
  m_alpha = 1;
  m_ecnCnt = 0;
  m_batchSizeOfAlpha = 0;
}

void Dctcp::HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  uint64_t ack_seq = ch.ack.seq;
  uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
  bool new_batch = false;

  // update alpha
  m_ecnCnt += (cnp > 0);
  if (ack_seq >
      m_lastUpdateSeq) { // if full RTT feedback is ready, do alpha update
#if PRINT_LOG
    printf(
        "%lu %s %08x %08x %u %u [%u,%u,%u] %.3lf->",
        Simulator::Now().GetTimeStep(),
        "alpha",
        qp->sip.Get(),
        qp->dip.Get(),
        qp->sport,
        qp->dport,
        m_lastUpdateSeq,
        ch.ack.seq,
        qp->snd_nxt,
        m_alpha);
#endif
    new_batch = true;
    if (m_lastUpdateSeq == 0) { // first RTT
      m_lastUpdateSeq = qp->snd_nxt;
      m_batchSizeOfAlpha = qp->snd_nxt / rdmaHw->m_mtu + 1;
    } else {
      double frac = std::min(1.0, double(m_ecnCnt) / m_batchSizeOfAlpha);
      m_alpha = (1 - m_g) * m_alpha + m_g * frac;
      m_lastUpdateSeq = qp->snd_nxt;
      m_ecnCnt = 0;
      m_batchSizeOfAlpha = (qp->snd_nxt - ack_seq) / rdmaHw->m_mtu + 1;
#if PRINT_LOG
      printf("%.3lf F:%.3lf", m_alpha, frac);
#endif
    }
#if PRINT_LOG
    printf("\n");
#endif
  }

  // check cwr exit
  if (m_caState == 1) {
    if (ack_seq > m_highSeq)
      m_caState = 0;
  }

  // check if need to reduce rate: ECN and not in CWR
  if (cnp && m_caState == 0) {
#if PRINT_LOG
    printf(
        "%lu %s %08x %08x %u %u %.3lf->",
        Simulator::Now().GetTimeStep(),
        "rate",
        qp->sip.Get(),
        qp->dip.Get(),
        qp->sport,
        qp->dport,
        m_ccRate.GetBitRate() * 1e-9);
#endif
    ChangeRate(m_ccRate * (1 - m_alpha / 2));
#if PRINT_LOG
    printf("%.3lf\n", m_ccRate.GetBitRate() * 1e-9);
#endif
    m_caState = 1;
    m_highSeq = qp->snd_nxt;
  }

  // additive inc
  if (m_caState == 0 && new_batch)
    ChangeRate(m_ccRate + m_dctcp_rai);
}

/*********************
 * HPCC-PINT
 ********************/
NS_OBJECT_ENSURE_REGISTERED(HpccPint);

TypeId HpccPint::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::HpccPint")
          .SetParent<Hpcc>()
          .AddConstructor<HpccPint>()
          .AddAttribute(
              "PintProb",
              "PINT's probability, using to calculate PintSmplThresh, which is PINT's sampling threshold in rand()%65536",
              DoubleValue(1.0),
              MakeDoubleAccessor(&HpccPint::m_pint_prob),
              MakeDoubleChecker<double>());
  return tid;
}

HpccPint::HpccPint() {
  m_lastUpdateSeq = 0;
  m_incStage = 0;
  m_pint_smpl_thresh = (uint32_t)(65536 * m_pint_prob);
}
void HpccPint::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_curRate = lazyInitRate;
}

void HpccPint::HandleAck(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch) {
  uint64_t ack_seq = ch.ack.seq;
  if (rand() % 65536 >= m_pint_smpl_thresh)
    return;
  // update rate
  if (ack_seq >
      m_lastUpdateSeq) { // if full RTT feedback is ready, do full update
    UpdateRate(qp, p, ch, false);
  } else { // do fast react
    UpdateRate(qp, p, ch, true);
  }
}

void HpccPint::UpdateRate(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch,
    bool fast_react) {
  uint64_t next_seq = qp->snd_nxt;
  if (m_lastUpdateSeq == 0) { // first RTT
    m_lastUpdateSeq = next_seq;
  } else {
    // check packet INT
    IntHeader& ih = ch.ack.ih;
    double U = Pint::decode_u(ih.GetPower());

    DataRate new_rate;
    int32_t new_incStage;
    double max_c = U / m_targetUtil;

    if (max_c >= 1 || m_incStage >= m_miThresh) {
      new_rate = m_curRate / max_c + rdmaHw->m_rai;
      new_incStage = 0;
    } else {
      new_rate = m_curRate + rdmaHw->m_rai;
      new_incStage = m_incStage + 1;
    }
    if (new_rate < rdmaHw->m_minRate)
      new_rate = rdmaHw->m_minRate;
    if (new_rate > qp->m_max_rate)
      new_rate = qp->m_max_rate;
    ChangeRate(new_rate);
    if (!fast_react) {
      m_curRate = new_rate;
      m_incStage = new_incStage;
    }
    if (!fast_react) {
      if (next_seq > m_lastUpdateSeq)
        m_lastUpdateSeq = next_seq; //+ rand() % 2 * m_mtu;
    }
  }
}

/*********************
 * Swift
 ********************/
NS_OBJECT_ENSURE_REGISTERED(Swift);

TypeId Swift::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::Swift")
                          .SetParent<RdmaCongestionOps>()
                          .AddConstructor<Swift>()
                          .AddAttribute(
                              "BaseTarget",
                              "Base target delay of Swift (ns)",
                              UintegerValue(25000),
                              MakeUintegerAccessor(&Swift::m_baseTarget),
                              MakeUintegerChecker<uint64_t>())
                          .AddAttribute(
                              "Beta",
                              "The multiplicative decrement factor of Swift",
                              DoubleValue(0.8),
                              MakeDoubleAccessor(&Swift::m_mdFactor),
                              MakeDoubleChecker<double>())
                          .AddAttribute(
                              "MaxMdf",
                              "The max multiplicative decrement factorof Swift",
                              DoubleValue(0.4),
                              MakeDoubleAccessor(&Swift::m_maxMdFactor),
                              MakeDoubleChecker<double>())
                          .AddAttribute(
                              "Range",
                              "fs_range of Swift",
                              DoubleValue(4.0),
                              MakeDoubleAccessor(&Swift::m_range),
                              MakeDoubleChecker<double>());
  return tid;
}

Swift::Swift() {
  m_gamma = 0;
  m_canDecrease = true;
}

void Swift::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  if (m_gamma == 0) {
    m_gamma = 1. /
        (1. / std::sqrt(rdmaHw->m_minRate.GetBitRate()) -
         1. /
             std::sqrt(
                 qp->m_max_rate.GetBitRate())); // 1/(1/√minrate - 1/√maxrate)
  }
  m_curRate = lazyInitRate;
}

void Swift::HandleAck(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader& ch) {
  uint64_t rtt = Simulator::Now().GetTimeStep() - ch.ack.ih.ts;
  uint64_t target = qp->m_baseRtt;
  double range = m_range * m_gamma *
      (1. / std::sqrt(m_curRate.GetBitRate()) -
       1. / std::sqrt(qp->m_max_rate.GetBitRate()));
  if (range > m_range) {
    range = m_range;
  } else if (range < 0.) {
    range = 0.;
  }
  range += 1.;
  target += m_baseTarget * range;

  if (rtt < target) {
    // AI
    m_curRate += rdmaHw->m_rai;
    if (m_curRate < rdmaHw->m_minRate) {
      m_curRate = rdmaHw->m_minRate;
    } else if (m_curRate > qp->m_max_rate) {
      m_curRate = qp->m_max_rate;
    }
    ChangeRate(m_curRate);
  } else if (m_canDecrease) {
    // MD
    m_canDecrease = false;
    // m_canDecreaseTimer.SetDelay(NanoSeconds(rtt));
    // m_canDecreaseTimer.Schedule();

    m_eventSetCanDecrease = Simulator::Schedule(
        NanoSeconds(rtt), &Swift::SetCanDecreaseToTrue, this);

    double f = m_mdFactor * (rtt - target) / (double)rtt;
    if (f > m_maxMdFactor) {
      f = m_maxMdFactor;
    }

    m_curRate *= (1 - f);
    if (m_curRate < rdmaHw->m_minRate) {
      m_curRate = rdmaHw->m_minRate;
    } else if (m_curRate > qp->m_max_rate) {
      m_curRate = qp->m_max_rate;
    }
    ChangeRate(m_curRate);
  }
}

void Swift::AllComplete() {
  Simulator::Cancel(m_eventSetCanDecrease);
  RdmaCongestionOps::AllComplete();
}

/******************************
 * DCQCN (perQP & perIP)
 *****************************/
NS_OBJECT_ENSURE_REGISTERED(RealDcqcn);

TypeId RealDcqcn::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::RealDcqcn")
          .SetParent<RdmaCongestionOps>()
          .AddConstructor<RealDcqcn>()
          .AddAttribute(
              "EwmaGain",
              "Control gain parameter which determines the level of rate decrease",
              DoubleValue(1.0 / 16),
              MakeDoubleAccessor(&RealDcqcn::m_g),
              MakeDoubleChecker<double>())
          .AddAttribute(
              "BytesThreshold",
              "DCQCN's BytesThreshold",
              UintegerValue(10 * 1024 * 1024),
              MakeUintegerAccessor(&RealDcqcn::m_bytesThreshold),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "F",
              "DCQCN's F",
              UintegerValue(5),
              MakeUintegerAccessor(&RealDcqcn::m_F),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "AlphaUpdateDelay",
              "DCQCN's alpha update delay",
              TimeValue(MicroSeconds(55)),
              MakeTimeAccessor(&RealDcqcn::m_alphaUpdateDelay),
              MakeTimeChecker())
          .AddAttribute(
              "RateUpdateDelay",
              "DCQCN's rate update delay",
              TimeValue(MicroSeconds(55)),
              MakeTimeAccessor(&RealDcqcn::m_rateUpdateDelay),
              MakeTimeChecker())
          .AddAttribute(
              "Clamp",
              "If set, whenever a CNP is processed will cause the clamp",
              BooleanValue(false),
              MakeBooleanAccessor(&RealDcqcn::m_clampTargetRate),
              MakeBooleanChecker())
          .AddAttribute(
              "ClampAfterTimeInc",
              "Clamp only if rate was increased due to the timer",
              BooleanValue(true),
              MakeBooleanAccessor(&RealDcqcn::m_clampTargetRateAfterTimeInc),
              MakeBooleanChecker());
  return tid;
}

RealDcqcn::RealDcqcn() {
  m_alpha = 1;
  m_bytesCounter = 0;
  m_rateUpdateIter = 0;
  m_bytesUpdateIter = 0;
  m_first_cnp = true;
}

void RealDcqcn::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_targetRate = lazyInitRate;
  m_maxRate = lazyInitRate;
  m_lastUpdateRateTime = Simulator::Now();
}

void RealDcqcn::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p) {
  m_bytesCounter += p->GetSize();
  if (m_bytesCounter >= m_bytesThreshold) {
    m_bytesCounter = 0;
    m_bytesUpdateIter++;
    RampUp();
  }
  Time step = Simulator::Now() - m_lastUpdateRateTime;
  if (step >= m_rateUpdateDelay) {
    m_lastUpdateRateTime = Simulator::Now();
    uint32_t delta = step.GetTimeStep() / m_rateUpdateDelay.GetTimeStep();
    while(delta >0){
      delta--;
      m_rateUpdateIter++;
      RampUp();
    }
  }
}

void RealDcqcn::HandleAck(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch) {
  uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
  if (cnp) {
    // m_eventUpdateRate.Cancel();
    // re-schedule
    // m_eventUpdateRate =
    //     Simulator::Schedule(m_rateUpdateDelay, &RealDcqcn::RateTrigger, this);
    //clamp
    if(m_clampTargetRate || (m_clampTargetRateAfterTimeInc && m_rateUpdateIter > 0))
      m_targetRate = m_ccRate;
    // reduce rate
    ChangeRate(m_ccRate * (1 - m_alpha / 2));

    // clear iter
    m_bytesCounter = 0;
    m_rateUpdateIter = 0;
    m_bytesUpdateIter = 0;
    // update alpha
    if(m_first_cnp){
      m_first_cnp = false;
      m_lastUpdateAlphaTime = Simulator::Now();
    }else{
      Time step = Simulator::Now() - m_lastUpdateAlphaTime;
      if(step.GetTimeStep() > m_alphaUpdateDelay.GetTimeStep()){
        m_lastUpdateAlphaTime = Simulator::Now();
        uint32_t delta = step.GetTimeStep() / m_alphaUpdateDelay.GetTimeStep();
        if(delta > 512){
          m_alpha = m_g;
        }else{
          m_alpha = pow(1 - m_g, delta) * m_alpha + m_g;
        }
      }
    }
  }
}

void RealDcqcn::RampUp() {
  if (m_rateUpdateIter > m_F && m_bytesUpdateIter > m_F) { // Hyper increase
    uint32_t i = std::min(m_rateUpdateIter, m_bytesUpdateIter) - m_F + 1;
    m_targetRate = std::min(m_targetRate + i * rdmaHw->m_rhai, m_maxRate);
  } else if (m_rateUpdateIter >= m_F || m_bytesUpdateIter >= m_F) { // Additive increase
    m_targetRate = std::min(m_targetRate + rdmaHw->m_rai, m_maxRate);
  }
  // else m_rateUpdateIter < m_F && m_bytesUpdateIter < m_F
  // Fast recovery: don't need to update target rate
  ChangeRate((m_targetRate + m_ccRate) / 2);
}

/******************************
 * HPCC-ECN
 *****************************/
NS_OBJECT_ENSURE_REGISTERED(HpccEcn);

TypeId HpccEcn::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::HpccEcn")
          .SetParent<RdmaCongestionOps>()
          .AddConstructor<HpccEcn>()
          .AddAttribute(
              "DecreaseShift",
              "Rate would reduce 1>>DecreaseShift",
              UintegerValue(4),
              MakeUintegerAccessor(&HpccEcn::m_decreaseShift),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "RampUpTimer",
              "The time interval of control loop",
              TimeValue(MicroSeconds(8)),
              MakeTimeAccessor(&HpccEcn::m_rampUpTimer),
              MakeTimeChecker())
          .AddAttribute(
              "FastRecoveryThreshold",
              "Fast recovery threshold",
              UintegerValue(5),
              MakeUintegerAccessor(&HpccEcn::m_fastRecoveryThreshold),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "AiStageThreshold",
              "Additive increase stage threshold",
              UintegerValue(200),
              MakeUintegerAccessor(&HpccEcn::m_aiStageThresold),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "ResetTimer",
              "The time interval to reset context",
              TimeValue(MicroSeconds(160)),
              MakeTimeAccessor(&HpccEcn::m_resetTimer),
              MakeTimeChecker());
  return tid;
}

HpccEcn::HpccEcn() {
  // do nothing
}

void HpccEcn::LazyInit(Ptr<RdmaQueuePair> qp, DataRate lazyInitRate) {
  RdmaCongestionOps::LazyInit(qp, lazyInitRate);
  m_maxRate = lazyInitRate;
  DoInit();
}

void HpccEcn::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> p) {
  Time elapsed = Simulator::Now() - m_rampUpTimeStamp;
  if (elapsed >= m_rampUpTimer) {
    if (elapsed < m_resetTimer) {
      if (m_cnpCounter == 0) { // no cnp, do ramp up
        RampUp();
      }
      m_rampUpTimeStamp = Simulator::Now();
      m_cnpCounter = 0; // clear cnpCounter for this interval
    } else { // reset
      // std::string log = "At " + std::to_string(Simulator::Now().GetTimeStep()) +
      //     " node " + std::to_string(nic->GetNode()->GetId()) +
      //     " doInit with elpased " + std::to_string(elapsed.GetTimeStep()) +
      //     " rampUpTimeStamp " +
      //     std::to_string(m_rampUpTimeStamp.GetTimeStep()) + "\n";
      // std::cout << log;
      DoInit();
    }
  }
}

void HpccEcn::HandleAck(
    Ptr<RdmaQueuePair> qp,
    Ptr<Packet> p,
    CustomHeader& ch) {
  uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
  if (cnp) {
    m_cnpCounter++;
    m_rampUpStage = 0;
    if (m_cnpCounter == 1) { // reduce rate only once in a loop
      if (m_fastRecoveryStage > 0) {
        m_lastGoodRate = m_ccRate;
        m_fastRecoveryStage = 0;
      }
      // do reduce
      ChangeRate(m_ccRate - m_ccRate / double(1 << m_decreaseShift));
    }
  }
}

void HpccEcn::RampUp() {
  if (m_fastRecoveryStage <= m_fastRecoveryThreshold) {
    // do fast recovery
    m_fastRecoveryStage++;
    ChangeRate((m_lastGoodRate + m_ccRate) / 2);
  } else {
    // do AI
    if (m_rampUpStage <= m_aiStageThresold) {
      m_rampUpStage++;
      ChangeRate(m_ccRate + rdmaHw->m_rai);
    } else {
      ChangeRate(m_ccRate + rdmaHw->m_rhai);
    }
  }
}

void HpccEcn::DoInit() {
  m_ccRate = m_maxRate;
  m_lastGoodRate = m_maxRate;
  m_rampUpTimeStamp = Simulator::Now();
  m_cnpCounter = 0;
  m_rampUpStage = 0;
  m_fastRecoveryStage = 0;
}

} // namespace ns3