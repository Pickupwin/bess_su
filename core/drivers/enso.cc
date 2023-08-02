

#include "enso.h"
#include <arpa/inet.h>

using enso::Device;
using enso::RxPipe;
using enso::TxPipe;

const uint32_t kBaseIpAddress = ntohl(inet_addr("192.168.0.0"));
const uint32_t kDstPort = 80;
const uint32_t kProtocol = 0x11;

CommandResponse ENSOPort::Init(const bess::pb::ENSOPortArg &arg) {
	
	// TODO: retrive core_id & BaseIp from Arg
	uint32_t core_id = 0;
	uint32_t BaseIpAddress = arg.base_ip_addr();
	if (BaseIpAddress == 0){
		BaseIpAddress = kBaseIpAddress;
	}
	
	int num_txq = num_queues[PACKET_DIR_OUT];
	int num_rxq = num_queues[PACKET_DIR_INC];
	
	if(num_txq<0 || num_rxq<0){
		return CommandFailure(EINVAL, "Invalid number of RX/TX queues");
	}
	
	// Setup class member: std::unique_ptr<Device> dev_;
	dev_ = Device::Create(num_rxq, core_id);
	if(!dev_){
		return CommandFailure(ENODEV, "Device creation failed");
	}
	
	// Setup class member: std::vector<RxPipe*> rx_pipes_;
	for(int i=0;i<num_rxq;++i){
		RxPipe* rx_pipe = dev_->AllocateRxPipe();
		if(!rx_pipe){
			return CommandFailure(ENODEV, "RxPipe creation failed");
		}
		uint32_t dst_ip = BaseIpAddress + i;
		rx_pipe->Bind(kDstPort, 0, dst_ip, 0, kProtocol);
		rx_pipes_.push_back(rx_pipe);
	}
	
	// Setup class member: std::vector<TxPipe*> tx_pipes_;
	for(int i=0;i<num_txq;++i){
		TxPipe* tx_pipe = dev_->AllocateTxPipe();
		if(!tx_pipe){
			return CommandFailure(ENODEV, "TxPipe creation failed");
			
		}
		tx_pipes_.push_back(tx_pipe);
	}
	
	return CommandSuccess();
}

void ENSOPort::DeInit() {
	// Nothing to dealloc.
}

int ENSOPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
	// TODO: assert qid < #rx_pipes_;
	auto& rx_pipe = rx_pipes_[qid];
	auto batch = rx_pipe->RecvPkts(cnt);	//non-blocking
	
	if(batch.available_bytes() == 0) {
		return 0;
	}
	
	int recv_cnt = 0;
	
	for(auto enso_pkt : batch) {
		uint16_t pkt_len = enso::get_pkt_len(enso_pkt);
		// TODO: alloc bess pkts one by one can be slow;
		bess::Packet* bess_pkt = current_worker.packet_pool()->Alloc();
		if(!bess_pkt){
			break;
		}
		bess::utils::CopyInlined(bess_pkt->append(pkt_len), enso_pkt, pkt_len, true);	// TODO: replace with enso::memcpy
		bess_pkt->set_nb_segs(1);	//TODO: handle pkt_len > bess::Packet capacity.
		pkts[recv_cnt++] = bess_pkt;
	}
	
	rx_pipe->Clear();
	
	return recv_cnt;
}

static void GatherData(u_char * data, bess::Packet* pkt) {
	while(pkt) {
		bess::utils::CopyInlined(data, pkt->head_data(), pkt->head_len());
		data += pkt->head_len();
		pkt = reinterpret_cast<bess::Packet*>(pkt->next());
	}
}

int ENSOPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
	// TODO: assert qid < #tx_pipes_;
	auto& tx_pipe = tx_pipes_[qid];
	
	int sent = 0;
	
	while(sent < cnt) {
		bess::Packet* sbuf = pkts[sent++];
		int pkt_len = sbuf->total_len();
		// TODO: alloc all buf at once?
		uint8_t* tx_buf = tx_pipe->AllocateBuf(pkt_len);	//TODO: alloc zero
		
		GatherData(tx_buf, sbuf);
		tx_pipe->SendAndFree(pkt_len);
	}
	
	bess::Packet::Free(pkts, sent);
	
	return sent;
}

