#ifndef SWITCH_NODE_H
#define SWITCH_NODE_H

#include <unordered_map>
#include <ns3/node.h>
#include "qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"

namespace ns3 {

class Packet;

class SwitchNode : public Node{
	static const uint32_t pCnt = 1025;	// Number of ports used
	static const uint32_t qCnt = 8;	// Number of queues/priorities used
	uint32_t m_ecmpSeed;
	std::unordered_map<uint32_t, std::vector<int> > m_rtTable; // map from ip address (u32) to possible ECMP port (index of dev)
	// --------------------------------------------------------------------------
	// 在智算服务器中 有的服务器是8GPU对应4张网卡 但是我们在NS3中实现的是每个GPU为一个Node 所以不可能两个GPU公用一张网卡
	// 所以在这种情况下 目前的实现是 如果是8GPU对应4网卡的机型 每个GPU的网卡带宽是原本网卡带宽的一半
	// 这种实现保证了实际场景与模拟场景的理论带宽上限是一致的 但是忽略了交换机的行为的不同————在实际情况
	std::unordered_map<uint32_t, uint32_t> m_ip_address_map;
	// For ECMP hash observation

	std::unordered_map<int, int> m_out_idx_to_next_node_map;
	// five tuple
	std::unordered_map<std::string, int > m_5tuple_to_outDevIdx_map;
	std::unordered_map<std::string, int > m_5tuple_to_next_node_map;
	std::string GetFiveTupleFromPacket(CustomHeader &ch);
	// --------------------------------------------------------------------------
	std::set<uint32_t> active_ports;	// record active ports in switch

	// monitor of PFC
	uint32_t m_bytes[pCnt][pCnt][qCnt]; // m_bytes[inDev][outDev][qidx] is the bytes from inDev enqueued for outDev at qidx
	
	uint64_t m_txBytes[pCnt]; // counter of tx bytes

	uint32_t m_lastPktSize[pCnt];
	uint64_t m_lastPktTs[pCnt]; // ns
	double m_u[pCnt];

protected:
	bool m_ecnEnabled;
	bool m_pfcEnabled;
	uint32_t m_ccMode;
	uint64_t m_maxRtt;

	uint32_t m_ackHighPrio; // set high priority for ACK/NACK

private:
	int GetOutDev(Ptr<const Packet>, CustomHeader &ch);
	void SendToDev(Ptr<Packet>p, CustomHeader &ch);
	static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);
	void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
	void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);
public:
	Ptr<SwitchMmu> m_mmu;

	static TypeId GetTypeId (void);
	SwitchNode();
	void SetEcmpSeed(uint32_t seed);
	void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
	void AddIntf2NodeMap(int intf_idx, int nodeId);
	void ClearTable();
	bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
	void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

	// for approximate calc in PINT
	int logres_shift(int b, int l);
	int log2apprx(int x, int b, int m, int l); // given x of at most b bits, use most significant m bits of x, calc the result in l bits
	
	// for monitor
	uint64_t last_txBytes[pCnt]; // last sampling of the counter of tx bytes
	uint64_t last_port_qlen[pCnt]; // last sampling of the port length
	
	/**
	 * outoput format:
	 * time, sw_id, port_id, q_id, qlen, port_len
	 */
	void PrintSwitchQlen(FILE* qlen_output);
	/**
	 * outoput format:
	 * time, sw_id, port_id, txBytes
	 */
	void PrintSwitchBw(FILE* bw_output, uint32_t bw_mon_interval);
};

} /* namespace ns3 */

#endif /* SWITCH_NODE_H */
