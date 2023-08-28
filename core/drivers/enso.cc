

#include "enso.h"
#include <arpa/inet.h>

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"
#include "../utils/icmp.h"

using enso::Device;
using enso::RxPipe;
using enso::TxPipe;

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Udp;
using bess::utils::Tcp;
using bess::utils::Icmp;

CommandResponse ENSOPort::Init(const bess::pb::ENSOPortArg &arg) {
	
	zero_copy_ = arg.zero_copy();
	
	int num_txq = num_queues[PACKET_DIR_OUT];
	int num_rxq = num_queues[PACKET_DIR_INC];
	
	if(num_txq<0 || num_rxq<0){
		return CommandFailure(EINVAL, "Invalid number of RX/TX queues");
	}
	if (zero_copy_ && (num_rxq!=num_txq)) {
		return CommandFailure(EINVAL, "Invalid number of RX/TX queues");
	}
	
	// Setup class member: std::unique_ptr<Device> dev_;
	dev_ = Device::Create();
	if(!dev_){
		return CommandFailure(ENODEV, "Device creation failed");
	}
	
	if (zero_copy_) {
		
		for (int i=0; i<num_rxq && i<num_txq ;++i) {
			RxTxPipe *rx_tx_pipe = dev_->AllocateRxTxPipe(true);
			if(!rx_tx_pipe){
				return CommandFailure(ENODEV, "RxTxPipe creation failed");
			}
			rx_tx_pipes_.push_back(rx_tx_pipe);
		}
		
		LOG(INFO) << "Enso Init: (copy) RXQ " << num_rxq << ", TXQ " << num_txq;
		
		return CommandSuccess();
	}
	else {
		// Setup class member: std::vector<RxPipe*> rx_pipes_;
		for(int i=0;i<num_rxq;++i){
			RxPipe* rx_pipe = dev_->AllocateRxPipe(true);	// fallback rx queues.
			if(!rx_pipe){
				return CommandFailure(ENODEV, "RxPipe creation failed");
			}
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
		
		LOG(INFO) << "Enso Init: (copy) RXQ " << num_rxq << ", TXQ " << num_txq;
		
		return CommandSuccess();
	}

}

void ENSOPort::DeInit() {
	// Nothing to dealloc.
	
	LOG(INFO) << "Enso DeInit";
}

// RecvPkts approach 1
int ENSOPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
	
	
	if (zero_copy_) {
		
		uint16_t header_len = sizeof(Ethernet)+sizeof(Ipv4)+sizeof(Udp);
		
		auto &pipe = rx_tx_pipes_[qid];
		auto batch = pipe -> PeekPkts(cnt);
		
		if(batch.available_bytes() == 0) {
			return 0;
		}
		
		int recv_cnt = 0;
		
		for(auto enso_pkt : batch) {
			uint16_t pkt_len = enso::get_pkt_len(enso_pkt);
			if (pkt_len > header_len)	pkt_len = header_len;
			// TODO: alloc bess pkts one by one can be slow;
			bess::Packet* bess_pkt = current_worker.packet_pool()->Alloc();
			if(!bess_pkt){
				break;
			}
			// TODO: replace with enso::memcpy
			bess::utils::CopyInlined(bess_pkt->append(header_len+sizeof(queue_t)+sizeof(uint8_t*)), enso_pkt, pkt_len, true);
			queue_t *qid_pkt = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len;
			*qid_pkt = qid;
			void *pkt_base = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len + sizeof(queue_t);
			*(reinterpret_cast<uint8_t **>(pkt_base)) = enso_pkt;
			bess_pkt->set_nb_segs(1);	//TODO: handle pkt_len > bess::Packet capacity.
			pkts[recv_cnt++] = bess_pkt;
		}
		
		uint32_t batch_length = batch.processed_bytes();
		pipe->ConfirmBytes(batch_length);
		
		// LOG(INFO) << "Queue " << (int)qid << "Recv " << recv_cnt << " pkts.";
		
		return recv_cnt;
		
	}
	else {

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
			// TODO: replace with enso::memcpy
			bess::utils::CopyInlined(bess_pkt->append(pkt_len), enso_pkt, pkt_len, true);
			bess_pkt->set_nb_segs(1);	//TODO: handle pkt_len > bess::Packet capacity.
			pkts[recv_cnt++] = bess_pkt;
		}
		
		rx_pipe->Clear();
		
		return recv_cnt;
		
	}
	
}

static void GatherData(u_char * data, bess::Packet* pkt) {
	while(pkt) {
		// TODO: replace with enso::memcpy
		bess::utils::CopyInlined(data, pkt->head_data(), pkt->head_len());
		data += pkt->head_len();
		pkt = reinterpret_cast<bess::Packet*>(pkt->next());
	}
}

// SendPkts approach 2
int ENSOPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
	
	if (zero_copy_) {
		
		// LOG(INFO) << "Queue " << (int)(qid) << " try send " << cnt << " pkts.";
		uint16_t header_len = sizeof(Ethernet)+sizeof(Ipv4)+sizeof(Udp);
		bess::Packet *bess_pkt;
		queue_t *qid_pkt;
		// for(int i=0;i<cnt;++i)
		// 	for(int j=1;j<cnt;++j){
		// 		queue_t qid_0, qid_1;
		// 		bess_pkt = pkts[j-1];
		// 		qid_pkt = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len;
		// 		qid_0 = *qid_pkt;
		// 		bess_pkt = pkts[j];
		// 		qid_pkt = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len;
		// 		qid_1 = *qid_pkt;
		// 		if(qid_0>qid_1)
		// 			std::swap(pkts[j-1], pkts[j]);
		// 	}
		
		int sent = 0;
		queue_t cur_qid;
		bess_pkt = pkts[0];
		qid_pkt = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len;
		cur_qid = *qid_pkt;
		for(int i=1;i<cnt;++i){
			bess_pkt = pkts[i];
			qid_pkt = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len;
			if(cur_qid!=*qid_pkt){
				// work on queue cur_qid;
				// LOG(INFO) << "Work on " << (int)(cur_qid);
				// assert (sent<i)
				auto &pipe = rx_tx_pipes_[cur_qid];
				uint32_t proc_bytes = 0;
				while(sent<i){
					bess::Packet *bess_pkt = pkts[sent];
					void *pkt_base = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len + sizeof(queue_t);
					uint8_t *enso_pkt = *(reinterpret_cast<uint8_t **>(pkt_base));
					proc_bytes += ((enso::get_pkt_len(enso_pkt)+63)>>6)<<6;
					bess::utils::CopyInlined(enso_pkt, bess_pkt->head_data(), header_len);
					++sent;
				}
				// LOG(INFO) << "Q" << (int)cur_qid << "Process " << proc_bytes;
				pipe->SendAndFree(proc_bytes);
			}
			cur_qid = *qid_pkt;
		}
		do{
			int i=cnt;
			// work on queue cur_qid;
			// LOG(INFO) << "Work on " << (int)(cur_qid);
			// assert (sent<i)
			auto &pipe = rx_tx_pipes_[cur_qid];
			uint32_t proc_bytes = 0;
			while(sent<i){
				bess::Packet *bess_pkt = pkts[sent];
				void *pkt_base = reinterpret_cast<uint8_t *>(bess_pkt->head_data()) + header_len + sizeof(queue_t);
				uint8_t *enso_pkt = *(reinterpret_cast<uint8_t **>(pkt_base));
				proc_bytes += ((enso::get_pkt_len(enso_pkt)+63)>>6)<<6;
				bess::utils::CopyInlined(enso_pkt, bess_pkt->head_data(), header_len);
				++sent;
			}
			// LOG(INFO) << "Q" << (int)cur_qid << "Process " << proc_bytes;
			pipe->SendAndFree(proc_bytes);
		}while(false);
		
		bess::Packet::Free(pkts, cnt);
		
		return sent;
	}
	else{
		auto& tx_pipe = tx_pipes_[qid];
		
		uint8_t *tx_buf = tx_pipe->AllocateBuf(0);
		uint32_t tx_buf_size = tx_pipe->capacity();
		uint32_t batch_len = 0;
		
		int sent = 0;
		while(sent<cnt) {
			bess::Packet *sbuf = pkts[sent];
			int pkt_len = sbuf->total_len();
			if(tx_buf_size<(uint32_t)(pkt_len))	break;
			GatherData(tx_buf, sbuf);
			pkt_len = ((pkt_len+63)>>6)<<6;
			tx_buf += pkt_len;
			tx_buf_size -= pkt_len;
			batch_len += pkt_len;
			++sent;
		}
		
		tx_pipe->SendAndFree(batch_len);	// need to be 64-aligned.
		
		// bess::Packet::Free(pkts, sent);
		bess::Packet::Free(pkts, cnt);
		
		return sent;
	}
	
}

ADD_DRIVER(ENSOPort, "enso_port", "Enso driver")
