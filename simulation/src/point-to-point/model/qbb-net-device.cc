/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006 Georgia Tech Research Corporation, INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Yuliang Li <yuliangli@g.harvard.com>
 */

#define __STDC_LIMIT_MACROS 1
#include "ns3/qbb-net-device.h"
#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/cn-header.h"
#include "ns3/custom-header.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/error-model.h"
#include "ns3/flow-id-tag.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/object-vector.h"
#include "ns3/pause-header.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/pointer.h"
#include "ns3/ppp-header.h"
#include "ns3/qbb-channel.h"
#include "ns3/qbb-header.h"
#include "ns3/random-variable.h"
#include "ns3/red-queue.h"
#include "ns3/seq-ts-header.h"
#include "ns3/simple-drop-tail-queue.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
#include "ns3/uinteger.h"
#ifdef NS3_MTP
#include "ns3/mtp-interface.h"
#include "qbb-net-device.h"
#endif

namespace ns3 {
NS_LOG_COMPONENT_DEFINE("QbbNetDevice");

NS_OBJECT_ENSURE_REGISTERED(RdmaEgressQueue);

uint32_t RdmaEgressQueue::ack_q_idx = 3;
// RdmaEgressQueue
TypeId RdmaEgressQueue::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::RdmaEgressQueue")
          .SetParent<Object>()
          .AddTraceSource(
              "RdmaEnqueue",
              "Enqueue a packet in the RdmaEgressQueue.",
              MakeTraceSourceAccessor(&RdmaEgressQueue::m_traceRdmaEnqueue),
              "ns3::RdmaEgressQueue::RdmaEnqueue")
          .AddTraceSource(
              "RdmaDequeue",
              "Dequeue a packet in the RdmaEgressQueue.",
              MakeTraceSourceAccessor(&RdmaEgressQueue::m_traceRdmaDequeue),
              "ns3::RdmaEgressQueue::RdmaDequeue")
          .AddAttribute(
              "TokenPerRound",
              "Token per round in bytes",
              UintegerValue(4000),
              MakeUintegerAccessor(&RdmaEgressQueue::m_token_per_round),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TxDequeueMode",
              "The mode of TxDequeue",
              EnumValue(TxDequeueMode::DWRR),
              MakeEnumAccessor(&RdmaEgressQueue::m_txDequeueMode),
              MakeEnumChecker(
                  TxDequeueMode::DWRR, "DWRR",
                  TxDequeueMode::QP_AVAIL, "QpAvail"));
  return tid;
}

RdmaEgressQueue::RdmaEgressQueue() {
  m_rrlast = 0;
  m_qlast = 0;
  m_ackQ = CreateObject<SimpleDropTailQueue>();
  // m_ackQ = CreateObject<RedQueue>();
  m_ackQ->SetAttribute(
      "MaxBytes",
      UintegerValue(0xffffffff)); // queue limit is on a higher level, not here
}

std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>> RdmaEgressQueue::DoRoundRobin(
    bool paused[], int& state) {
  if (!paused[ack_q_idx] && m_ackQ->GetNPackets() > 0){ // high prio
    state = -1;
    m_qlast = -1;
    Ptr<Packet> p = m_ackQ->Dequeue();
    m_traceRdmaDequeue(p, 0);
    // there is no qp for high prio.
    return std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>>(nullptr, p);
  }
  if (m_qpGrp->m_qps.size() == 0){ // no qp
    state = -1024;
    // return -1024 with nullptr.
    return std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>>(nullptr, nullptr);
  }
  // no pkt in highest priority queue, check if rrlast still has tokens
  int res = -1024;
  Ptr<RdmaQueuePair> qplast = m_rrlast >= m_qpGrp->m_qps.size() ? nullptr : m_qpGrp->m_qps[m_rrlast];
  if (m_txDequeueMode == TxDequeueMode::DWRR && qplast && 
      qplast->m_tokenBucket.hasEnoughTokens(qplast->lastPktSize) &&
      !paused[qplast->m_pg] && qplast->GetBytesLeft() > 0 &&
      !qplast->IsWinBound()) {
    // dequeue rrlast
    Ptr<Packet> p = m_rdmaGetNxtPkt(qplast);
    m_qlast = m_rrlast;
    m_traceRdmaDequeue(p, qplast->m_pg);
    state = m_rrlast;
    return std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>>(qplast, p);
  }
  // else do rr for each qp
  uint32_t fcount = m_qpGrp->m_qps.size();
  uint32_t qIndex;
  for (qIndex = 1; qIndex <= fcount; qIndex++) {
    // iterate start behind the m_rrlast qp
    uint32_t idx = (qIndex + m_rrlast) % fcount;
    Ptr<RdmaQueuePair> qp = m_qpGrp->m_qps[idx];
    if (!paused[qp->m_pg] && qp->GetBytesLeft() > 0 && !qp->IsWinBound()) {
      if (m_txDequeueMode == TxDequeueMode::QP_AVAIL) {
        if (qp->m_nextAvail.GetTimeStep() <= Simulator::Now().GetTimeStep()) {
          res = idx;
          break;
        }
      } else if (m_txDequeueMode == TxDequeueMode::DWRR) {
        qp->m_tokenBucket.Update(Simulator::Now(), qp->m_rate);
        if (qp->m_tokenBucket.hasEnoughTokens(m_token_per_round)) {
          res = idx;
          break;
        }
      }
    }
  }
  if (res != -1024) {
    // dequeue res
    Ptr<RdmaQueuePair> qp = m_qpGrp->m_qps[res];
    Ptr<Packet> p = m_rdmaGetNxtPkt(qp);
    m_rrlast = res;
    m_qlast = res;
    m_traceRdmaDequeue(p, qp->m_pg);
    state = res;
    return std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>>(qp, p);
  }
  // no qp can be dequeued. return -1024 with nullptr.
  state = -1024;
  return std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>>(nullptr, nullptr);
}

int RdmaEgressQueue::GetLastQueue() {
  return m_qlast;
}

uint32_t RdmaEgressQueue::GetNBytes(uint32_t qIndex) {
  NS_ASSERT_MSG(
      qIndex < m_qpGrp->GetN(),
      "RdmaEgressQueue::GetNBytes: qIndex >= m_qpGrp->GetN()");
  return m_qpGrp->Get(qIndex)->GetBytesLeft();
}

uint32_t RdmaEgressQueue::GetFlowCount(void) {
  return m_qpGrp->GetN();
}

Ptr<RdmaQueuePair> RdmaEgressQueue::GetQp(uint32_t i) {
  if(i >= m_qpGrp->GetN()){
    return nullptr;
  }
  return m_qpGrp->Get(i);
}

void RdmaEgressQueue::RecoverQueue(uint32_t i) {
  NS_ASSERT_MSG(
      i < m_qpGrp->GetN(),
      "RdmaEgressQueue::RecoverQueue: qIndex >= m_qpGrp->GetN()");
  m_qpGrp->Get(i)->snd_nxt = m_qpGrp->Get(i)->snd_una;
}

void RdmaEgressQueue::EnqueueHighPrioQ(Ptr<Packet> p) {
  m_traceRdmaEnqueue(p, 0);
  m_ackQ->Enqueue(p);
}

void RdmaEgressQueue::CleanHighPrio(
    TracedCallback<Ptr<const Packet>, uint32_t> dropCb) {
  while (m_ackQ->GetNPackets() > 0) {
    Ptr<Packet> p = m_ackQ->Dequeue();
    dropCb(p, 0);
  }
}

void RdmaEgressQueue::RemoveFinishedQps() {
  m_qpGrp->RemoveFinishedQps(m_rrlast);
}

/******************
 * QbbNetDevice
 *****************/
NS_OBJECT_ENSURE_REGISTERED(QbbNetDevice);

TypeId QbbNetDevice::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::QbbNetDevice")
          .SetParent<PointToPointNetDevice>()
          .AddConstructor<QbbNetDevice>()
          .AddAttribute(
              "QbbEnabled",
              "Enable the generation of PAUSE packet.",
              BooleanValue(true),
              MakeBooleanAccessor(&QbbNetDevice::m_qbbEnabled),
              MakeBooleanChecker())
          .AddAttribute(
              "QcnEnabled",
              "Enable the generation of PAUSE packet.",
              BooleanValue(false),
              MakeBooleanAccessor(&QbbNetDevice::m_qcnEnabled),
              MakeBooleanChecker())
          .AddAttribute(
              "DynamicThreshold",
              "Enable dynamic threshold.",
              BooleanValue(false),
              MakeBooleanAccessor(&QbbNetDevice::m_dynamicth),
              MakeBooleanChecker())
          .AddAttribute(
              "PauseTime",
              "Number of microseconds to pause upon congestion",
              UintegerValue(5),
              MakeUintegerAccessor(&QbbNetDevice::m_pausetime),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TxBeQueue",
              "A queue to use as the transmit queue in the device.",
              PointerValue(),
              MakePointerAccessor(&QbbNetDevice::m_queue),
              MakePointerChecker<PacketQueue>())
          .AddAttribute(
              "RdmaEgressQueue",
              "A queue to use as the transmit queue in the device.",
              PointerValue(),
              MakePointerAccessor(&QbbNetDevice::m_rdmaEQ),
              MakePointerChecker<Object>())
          .AddAttribute(
              "NVLS_enable",
              "enable NVLS",
              UintegerValue(0),
              MakeUintegerAccessor(&QbbNetDevice::nvls_enable),
              MakeUintegerChecker<uint32_t>())
          .AddTraceSource(
              "QbbEnqueue",
              "Enqueue a packet in the QbbNetDevice.",
              MakeTraceSourceAccessor(&QbbNetDevice::m_traceEnqueue),
              "ns3::QbbEnabled::QbbEnqueue")
          .AddTraceSource(
              "QbbDequeue",
              "Dequeue a packet in the QbbNetDevice.",
              MakeTraceSourceAccessor(&QbbNetDevice::m_traceDequeue),
              "ns3::QbbEnabled::QbbDequeue")
          .AddTraceSource(
              "QbbDrop",
              "Drop a packet in the QbbNetDevice.",
              MakeTraceSourceAccessor(&QbbNetDevice::m_traceDrop),
              "ns3::QbbEnabled::QbbDrop")
          .AddTraceSource(
              "RdmaQpDequeue",
              "A qp dequeue a packet.",
              MakeTraceSourceAccessor(&QbbNetDevice::m_traceQpDequeue),
              "ns3::QbbEnabled::Dequeue")
          .AddTraceSource(
              "QbbPfc",
              "get a PFC packet. 0: resume, 1: pause",
              MakeTraceSourceAccessor(&QbbNetDevice::m_tracePfc),
              "ns3::QbbEnabled::QbbPfc");

  return tid;
}

QbbNetDevice::QbbNetDevice() {
  NS_LOG_FUNCTION(this);
  m_ecn_source = new std::vector<ECNAccount>;
  for (uint32_t i = 0; i < qCnt; i++) {
    m_paused[i] = false;
  }

  m_rdmaEQ = CreateObject<RdmaEgressQueue>();
}

QbbNetDevice::~QbbNetDevice() {
  NS_LOG_FUNCTION(this);
}

void QbbNetDevice::DoDispose() {
  NS_LOG_FUNCTION(this);
  PointToPointNetDevice::DoDispose();
}

void QbbNetDevice::TransmitComplete(void) {
  NS_LOG_FUNCTION(this);
  NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
  m_txMachineState = READY;
  NS_ASSERT_MSG(
      m_currentPkt != 0, "QbbNetDevice::TransmitComplete(): m_currentPkt zero");
  m_phyTxEndTrace(m_currentPkt);
  m_currentPkt = 0;
  DequeueAndTransmit();
}

void QbbNetDevice::SwitchAsHostTransmitComplete(void) {
  NS_LOG_FUNCTION(this);
  NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
  m_txMachineState = READY;
  NS_ASSERT_MSG(
      m_currentPkt != 0, "QbbNetDevice::TransmitComplete(): m_currentPkt zero");
  m_phyTxEndTrace(m_currentPkt);
  m_currentPkt = 0;
  SwitchAsHostSend();
}

void QbbNetDevice::DequeueAndTransmit(void) {
  NS_LOG_FUNCTION(this);
  if (!m_linkUp)
    return; // if link is down, return
#ifdef NS3_MTP
  MtpInterface::explicitCriticalSection cs;
#endif
  if (m_txMachineState == BUSY){
#ifdef NS3_MTP
    cs.ExitSection();
#endif
    return; // Quit if channel busy
  }
  if (m_node->GetNodeType() == 0 ||
      (m_node->GetNodeType() == 2 && nvls_enable == 1)) {
    // m_rdmaEQ->RemoveFinishedQps();
    int state = -1024;
    std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>> dequeuePair =
        m_rdmaEQ->DoRoundRobin(m_paused, state);
    if (state != -1024) { // has packet to send
      Ptr<Packet> p = dequeuePair.second;
      if (state == -1) { // high prio
        m_traceDequeue(p, 0);      
        TransmitStart(p);
#ifdef NS3_MTP
        cs.ExitSection();
#endif
      } else {
        Ptr<RdmaQueuePair> lastQp = dequeuePair.first;
        lastQp->m_tokenBucket.consumeTokens(p->GetSize());

        m_traceQpDequeue(p, lastQp);
        TransmitStart(p);
#ifdef NS3_MTP
        cs.ExitSection();
#endif
        m_rdmaPktSent(lastQp, p, m_tInterframeGap);
      }
      m_rdmaUpdateTxBytes(m_ifIndex, p->GetSize());
    } else { // no packet to send
      NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
      Time t = Simulator::GetMaximumSimulationTime();
      uint32_t fcount = m_rdmaEQ->m_qpGrp->m_qps.size();
      for(uint32_t i = 0; i < fcount; i++){
        Ptr<RdmaQueuePair> qp = m_rdmaEQ->m_qpGrp->m_qps[i];
        qp->UpdateRate();
        if (m_rdmaEQ->m_txDequeueMode == RdmaEgressQueue::TxDequeueMode::QP_AVAIL) {
          t = Min(qp->m_nextAvail, t);
        } else if (m_rdmaEQ->m_txDequeueMode == RdmaEgressQueue::TxDequeueMode::DWRR) {
          Time newt = qp->m_rate.CalculateBytesTxTime(
              (int64_t)m_rdmaEQ->m_token_per_round - qp->m_tokenBucket.m_tokens);
          newt = newt.GetTimeStep() == 0 ? NanoSeconds(1) : newt;
          t = Min(newt + Simulator::Now(), t);
        }
      }
#ifdef NS3_MTP
      cs.ExitSection();
#endif
      if(m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() &&
        t > Simulator::Now()){
          m_nextSend = Simulator::Schedule(
            t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
        }
    }
    return;
  } else { // switch, doesn't care about qcn, just send
    Ptr<Packet> p = m_queue->DequeueRR(m_paused); // this is round-robin
    if (p != 0) {
      m_snifferTrace(p);
      m_promiscSnifferTrace(p);
      Ipv4Header h;
      Ptr<Packet> packet = p->Copy();
      uint16_t protocol = 0;
      ProcessHeader(packet, protocol);
      packet->RemoveHeader(h);
    //   qbbHeader seqh;
    //   packet->PeekHeader(seqh);
    // std::string log = "At " + std::to_string(Simulator::Now().GetTimeStep()) +
    //     " node" + std::to_string(m_node->GetId()) +
    //     " sport= " + std::to_string(seqh.GetSport()) +
    //     " sip= " + std::to_string(h.GetSource().Get()) + "\n";
    // std::cout<<log;
      FlowIdTag t;
      uint32_t qIndex = m_queue->GetLastQueue();
      if (qIndex == 0) { // this is a pause or cnp, send it immediately!
        m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
        p->RemovePacketTag(t);
      } else {
        m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
        p->RemovePacketTag(t);
      }
      m_traceDequeue(p, qIndex);
      TransmitStart(p);
#ifdef NS3_MTP
      cs.ExitSection();
#endif
      return;
    }
    else{ // No queue can deliver any packet
      NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
    }
  }
#ifdef NS3_MTP
  cs.ExitSection();
#endif
  return;
}

void QbbNetDevice::SwitchAsHostSend(void) {
  NS_LOG_FUNCTION(this);
  if (!m_linkUp)
    return; // if link is down, return
#ifdef NS3_MTP
  MtpInterface::explicitCriticalSection cs;
#endif
  if (m_txMachineState == BUSY){
#ifdef NS3_MTP
    cs.ExitSection();
#endif
    return; // Quit if channel busy
  }

  // m_rdmaEQ->RemoveFinishedQps();
  int state = -1024;
  std::pair<Ptr<RdmaQueuePair>, Ptr<Packet>> dequeuePair =
      m_rdmaEQ->DoRoundRobin(m_paused, state);
  if (state != -1024) { // has packet to send
    Ptr<Packet> p = dequeuePair.second;
    if (state == -1) { // high prio
      m_traceDequeue(p, 0);      
      SwitchAsHostTransmitStart(p);
#ifdef NS3_MTP
      cs.ExitSection();
#endif
    } else {
      Ptr<RdmaQueuePair> lastQp = dequeuePair.first;
      NS_ASSERT_MSG(
      lastQp->nvls_enable == 1 && m_node->GetNodeType() == 2,
      "Switch as host send must with NVLS ON!");
    lastQp->m_tokenBucket.consumeTokens(p->GetSize());
    lastQp->m_tokenBucket.consumeTokens(p->GetSize());
    // update statistics for monitor
    m_rdmaUpdateTxBytes(m_ifIndex, p->GetSize());
    // transmit
      lastQp->m_tokenBucket.consumeTokens(p->GetSize());
    // update statistics for monitor
    m_rdmaUpdateTxBytes(m_ifIndex, p->GetSize());
    // transmit
      m_traceQpDequeue(p, lastQp);
      SwitchAsHostTransmitStart(p);
#ifdef NS3_MTP
      cs.ExitSection();
#endif
      m_rdmaPktSent(lastQp, p, m_tInterframeGap);
    }
    m_rdmaUpdateTxBytes(m_ifIndex, p->GetSize());
  } else { // no packet to send
    NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
    Time t = Simulator::GetMaximumSimulationTime();
    uint32_t fcount = m_rdmaEQ->m_qpGrp->m_qps.size();
    for(uint32_t i = 0; i < fcount; i++){
      Ptr<RdmaQueuePair> qp = m_rdmaEQ->m_qpGrp->m_qps[i];
      qp->UpdateRate();
      if (m_rdmaEQ->m_txDequeueMode == RdmaEgressQueue::TxDequeueMode::QP_AVAIL) {
        t = Min(qp->m_nextAvail, t);
      } else if (m_rdmaEQ->m_txDequeueMode == RdmaEgressQueue::TxDequeueMode::DWRR) {
        Time newt = qp->m_rate.CalculateBytesTxTime(
            (int64_t)m_rdmaEQ->m_token_per_round - qp->m_tokenBucket.m_tokens);
        newt = newt.GetTimeStep() == 0 ? NanoSeconds(1) : newt;
        t = Min(newt + Simulator::Now(), t);
      }
    }
#ifdef NS3_MTP
    cs.ExitSection();
#endif
    if(m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() &&
      t > Simulator::Now()){
        m_nextSend = Simulator::Schedule(
          t - Simulator::Now(), &QbbNetDevice::SwitchAsHostSend, this);
      }
  }
  return;
}

void QbbNetDevice::SwitchDequeueAndTransmit(void) {
  NS_LOG_FUNCTION(this);
  if (!m_linkUp)
    return; // if link is down, return
  if (m_txMachineState == BUSY)
    return; // Quit if channel busy
  Ptr<Packet> p;
  p = m_queue->DequeueRR(m_paused); // this is round-robin
  if (p != 0) {
    m_snifferTrace(p);
    m_promiscSnifferTrace(p);
    Ipv4Header h;
    Ptr<Packet> packet = p->Copy();
    uint16_t protocol = 0;
    ProcessHeader(packet, protocol);
    packet->RemoveHeader(h);
    // qbbHeader seqh;
    // packet->PeekHeader(seqh);
    // std::string log = "At " + std::to_string(Simulator::Now().GetTimeStep()) +
    //     " node" + std::to_string(m_node->GetId()) +
    //     " sport= " + std::to_string(seqh.GetSport()) +
    //     " sip= " + std::to_string(h.GetSource().Get()) + "\n";
    // std::cout<<log;
    FlowIdTag t;
    uint32_t qIndex = m_queue->GetLastQueue();
    if (qIndex == 0) { // this is a pause or cnp, send it immediately!
      m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
      p->RemovePacketTag(t);
    } else {
      m_node->SwitchNotifyDequeue(m_ifIndex, qIndex, p);
      p->RemovePacketTag(t);
    }
    m_traceDequeue(p, qIndex);
    TransmitStart(p);
    return;
  } else { // No queue can deliver any packet
    NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
    if (m_node->GetNodeType() == 0 &&
        m_qcnEnabled) { // nothing to send, possibly due to qcn flow control, if
                        // so reschedule sending
      Time t = Simulator::GetMaximumSimulationTime();
      for (uint32_t i = 0; i < m_rdmaEQ->GetFlowCount(); i++) {
        Ptr<RdmaQueuePair> qp = m_rdmaEQ->GetQp(i);
        t = Min(qp->m_nextAvail, t);
      }
      if (m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() &&
          t > Simulator::Now()) {
        m_nextSend = Simulator::Schedule(
            t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
      }
    }
  }
}

void QbbNetDevice::Resume(unsigned qIndex) {
  NS_LOG_FUNCTION(this << qIndex);
  NS_ASSERT_MSG(m_paused[qIndex], "Must be PAUSEd");
  m_paused[qIndex] = false;
  NS_LOG_INFO(
      "Node " << m_node->GetId() << " dev " << m_ifIndex << " queue " << qIndex
              << " resumed at " << Simulator::Now().GetSeconds());
  if(m_node->GetNodeType() == 1) { // switch
    DequeueAndTransmit();
    return;
  }
  Ptr<RdmaQueuePair> lastQp = m_rdmaEQ->GetQp(qIndex);
  if (lastQp != nullptr && lastQp->nvls_enable == 1 && m_node->GetNodeType() == 2)
    SwitchAsHostSend();
  else
    DequeueAndTransmit();
}

void QbbNetDevice::Receive(Ptr<Packet> packet) {
  NS_LOG_FUNCTION(this << packet);
  if (!m_linkUp) {
    m_traceDrop(packet, 0);
    return;
  }

  if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet)) {
    //
    // If we have an error model and it indicates that it is time to lose a
    // corrupted packet, don't forward this packet up, let it go.
    //
    m_phyRxDropTrace(packet);
    return;
  }

  m_macRxTrace(packet);
  CustomHeader ch(
      CustomHeader::L2_Header | CustomHeader::L3_Header |
      CustomHeader::L4_Header);
  ch.getInt = 1; // parse INT header
  packet->PeekHeader(ch);
  if (ch.l3Prot == 0xFE) { // PFC
    if (!m_qbbEnabled)
      return;
    unsigned qIndex = ch.pfc.qIndex;
    if (ch.pfc.time > 0) {
      m_tracePfc(1);
      m_paused[qIndex] = true;
    } else {
      m_tracePfc(0);
      Resume(qIndex);
    }
  } else { // non-PFC packets (data, ACK, NACK, CNP...)
    uint32_t sip = ch.sip;
    uint32_t sid = (sip >> 8) & 0xffff;
    uint32_t dip = ch.dip;
    uint32_t did = (dip >> 8) & 0xffff;
    if (m_node->GetNodeType() > 0 && ch.m_tos != 4 &&
        did != m_node->GetId()) { // switch
      // std::cout << "id: " << m_node->GetId() << " switch receive from " <<
      // sid << std::endl;
      packet->AddPacketTag(FlowIdTag(m_ifIndex));
      m_node->SwitchReceiveFromDevice(this, packet, ch);
    } else { // NIC
      // send to RdmaHw
      // std::cout << "id: " << m_node->GetId() << " NIC receive from " << sid
      // << std::endl;
      if (ch.l3Prot == 0xFC) {
      }
      int ret = m_rdmaReceiveCb(packet, ch);
      m_rdmaUpdateRxBytes(m_ifIndex, packet->GetSize());
      // TODO we may based on the ret do something
    }
  }
  return;
}

bool QbbNetDevice::Send(
    Ptr<Packet> packet,
    const Address& dest,
    uint16_t protocolNumber) {
  NS_ASSERT_MSG(false, "QbbNetDevice::Send not implemented yet\n");
  return false;
}

bool QbbNetDevice::SwitchSend(
    uint32_t qIndex,
    Ptr<Packet> packet,
    CustomHeader& ch) {
  m_macTxTrace(packet);
  m_traceEnqueue(packet, qIndex);
  m_queue->Enqueue(packet, qIndex);
  // DequeueAndTransmit();
  SwitchDequeueAndTransmit();
  return true;
}

void QbbNetDevice::SendPfc(uint32_t qIndex, uint32_t type) {
  Ptr<Packet> p = Create<Packet>(0);
  PauseHeader pauseh(
      (type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
  p->AddHeader(pauseh);
  Ipv4Header ipv4h; // Prepare IPv4 header
  ipv4h.SetProtocol(0xFE);
  ipv4h.SetSource(
      m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
  ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
  ipv4h.SetPayloadSize(p->GetSize());
  ipv4h.SetTtl(1);
  ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
  p->AddHeader(ipv4h);
  AddHeader(p, 0x800);
  CustomHeader ch(
      CustomHeader::L2_Header | CustomHeader::L3_Header |
      CustomHeader::L4_Header);
  p->PeekHeader(ch);
  SwitchSend(0, p, ch);
}

Ptr<Packet> QbbNetDevice::NICSendPfc(uint32_t qIndex, uint32_t type) {
  Ptr<Packet> p = Create<Packet>(0);
  PauseHeader pauseh(
      (type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
  p->AddHeader(pauseh);
  Ipv4Header ipv4h; // Prepare IPv4 header
  ipv4h.SetProtocol(0xFE);
  // ipv4h.SetProtocol(L3ProtType::kPFC);
  ipv4h.SetSource(
      m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
  ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
  ipv4h.SetPayloadSize(p->GetSize());
  ipv4h.SetTtl(1);
  ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
  p->AddHeader(ipv4h);
  AddHeader(p, 0x800);
  return p;
}

bool QbbNetDevice::Attach(Ptr<QbbChannel> ch) {
  NS_LOG_FUNCTION(this << &ch);
  m_channel = ch;
  m_channel->Attach(this);
  NotifyLinkUp();
  return true;
}
void QbbNetDevice::SendCallback(Ptr<Packet> packet) {
  CustomHeader ch(
      CustomHeader::L2_Header | CustomHeader::L3_Header |
      CustomHeader::L4_Header);
  packet->PeekHeader(ch);
  // std::cout << "before m_rdmaSentCb on id: " << m_node->GetId()
  //           << " value: " << std::endl;
  m_rdmaSentCb(packet, ch);
}
bool QbbNetDevice::TransmitStart(Ptr<Packet> p) {
  NS_LOG_FUNCTION(this << p);
  NS_LOG_LOGIC("UID is " << p->GetUid() << ")");
  //
  // This function is called to start the process of transmitting a packet.
  // We need to tell the channel that we've started wiggling the wire and
  // schedule an event that will be executed when the transmission is complete.
  //
  NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");
  m_txMachineState = BUSY;
  m_currentPkt = p;
  m_phyTxBeginTrace(m_currentPkt);
  Time txTime = m_bps.CalculateBytesTxTime(p->GetSize());
  // 添加当前qp所要发送的最后一个packet txtime后回调 根据mtu
  // 根据qpindex
  //  添加一个回调
  if (m_rdmaEQ != nullptr && m_rdmaEQ->m_qpGrp != nullptr &&
      m_node->GetNodeType() == 0) {
    if (p->GetSize() < 9000 &&
        p->GetSize() > 60) { // 增加判断当前packet是否是ack报文的逻辑。
      // if(lastQp->IsFinished()){s
      // Simulator::Schedule(txTime,&sendfinsh,this);
      CustomHeader ch(
          CustomHeader::L2_Header | CustomHeader::L3_Header |
          CustomHeader::L4_Header);
      // ch.getInt = 1; // parse INT header
      p->PeekHeader(ch);
      Simulator::Schedule(txTime, &QbbNetDevice::SendCallback, this, p);
    }
    // }
  }
  Time txCompleteTime = txTime + m_tInterframeGap;
  NS_LOG_LOGIC(
      "Schedule TransmitCompleteEvent in " << txCompleteTime.GetSeconds()
                                           << "sec");
  Simulator::Schedule(txCompleteTime, &QbbNetDevice::TransmitComplete, this);

  bool result = m_channel->TransmitStart(p, this, txTime);

  if (result == false) {
    m_phyTxDropTrace(p);
  }
  return result;
}

bool QbbNetDevice::SwitchAsHostTransmitStart(Ptr<Packet> p) {
  NS_LOG_FUNCTION(this << p);
  NS_LOG_LOGIC("UID is " << p->GetUid() << ")");
  //
  // This function is called to start the process of transmitting a packet.
  // We need to tell the channel that we've started wiggling the wire and
  // schedule an event that will be executed when the transmission is complete.
  //
  NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");
  m_txMachineState = BUSY;
  m_currentPkt = p;
  m_phyTxBeginTrace(m_currentPkt);
  Time txTime = m_bps.CalculateBytesTxTime(p->GetSize());
  if (m_rdmaEQ != nullptr && m_rdmaEQ->m_qpGrp != nullptr &&
      m_node->GetNodeType() == 2) {
    if (p->GetSize() < 9000 &&
        p->GetSize() > 60) { // 增加判断当前packet是否是ack报文的逻辑。
      CustomHeader ch(
          CustomHeader::L2_Header | CustomHeader::L3_Header |
          CustomHeader::L4_Header);
      // ch.getInt = 1; // parse INT header
      p->PeekHeader(ch);
      Simulator::Schedule(txTime, &QbbNetDevice::SendCallback, this, p);
    }
    // }
  }
  Time txCompleteTime = txTime + m_tInterframeGap;
  NS_LOG_LOGIC(
      "Schedule TransmitCompleteEvent in " << txCompleteTime.GetSeconds()
                                           << "sec");
  Simulator::Schedule(
      txCompleteTime, &QbbNetDevice::SwitchAsHostTransmitComplete, this);

  bool result = m_channel->TransmitStart(p, this, txTime);

  if (result == false) {
    m_phyTxDropTrace(p);
  }
  return result;
}

Ptr<Channel> QbbNetDevice::GetChannel(void) const {
  return m_channel;
}

bool QbbNetDevice::IsQbb(void) const {
  return true;
}

void QbbNetDevice::NewQp(Ptr<RdmaQueuePair> qp) {
  qp->m_nextAvail = Simulator::Now();
  if (qp->nvls_enable == 1 && m_node->GetNodeType() == 2)
    SwitchAsHostSend();
  else
    DequeueAndTransmit();
}
void QbbNetDevice::ReassignedQp(Ptr<RdmaQueuePair> qp) {
  DequeueAndTransmit();
}
void QbbNetDevice::TriggerTransmit() {
  DequeueAndTransmit();
}

void QbbNetDevice::SetQueue(Ptr<BEgressQueue> q) {
  NS_LOG_FUNCTION(this << q);
  m_queue = q;
}

Ptr<BEgressQueue> QbbNetDevice::GetQueue() {
  return m_queue;
}

Ptr<RdmaEgressQueue> QbbNetDevice::GetRdmaQueue() {
  return m_rdmaEQ;
}

void QbbNetDevice::RdmaEnqueueHighPrioQ(Ptr<Packet> p) {
  m_traceEnqueue(p, 0);
  m_rdmaEQ->EnqueueHighPrioQ(p);
}

void QbbNetDevice::TakeDown() {
  // TODO: delete packets in the queue, set link down
  if (m_node->GetNodeType() == 0) {
    // clean the high prio queue
    m_rdmaEQ->CleanHighPrio(m_traceDrop);
    // notify driver/RdmaHw that this link is down
    m_rdmaLinkDownCb(this);
  } else { // switch
    // clean the queue
    for (uint32_t i = 0; i < qCnt; i++)
      m_paused[i] = false;
    while (1) {
      Ptr<Packet> p = m_queue->DequeueRR(m_paused);
      if (p == 0)
        break;
      m_traceDrop(p, m_queue->GetLastQueue());
    }
    // TODO: Notify switch that this link is down
  }
  m_linkUp = false;
}

void QbbNetDevice::UpdateNextAvail(Time t) {
  if (!m_nextSend.IsExpired() && t < Time(m_nextSend.GetTs())) {
    Simulator::Cancel(m_nextSend);
    Time delta = t < Simulator::Now() ? Time(0) : t - Simulator::Now();
    m_nextSend =
        Simulator::Schedule(delta, &QbbNetDevice::DequeueAndTransmit, this);
  }
}
} // namespace ns3
