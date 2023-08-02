#ifndef BESS_DRIVERS_ENSO_H
#define BESS_DRIVERS_ENSO_H

#include "../port.h"


#include <enso/helpers.h>
#include <enso/pipe.h>




using enso::Device;
using enso::RxPipe;
using enso::TxPipe;

class ENSOPort final : public Port {
    
    public:
    ENSOPort()
        : Port(),
          dev(nullptr),
          rx_pipes(),
          tx_pipes() {}
        CommandResponse Init(const bess::pb::ENSOPortArg &arg);
        
        void DeInit() override;
        
        int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
        
        int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
    
    private:
        
        std::unique_ptr<Device> dev_;
        std::vector<RxPipe*> rx_pipes_;
        std::vector<TxPipe*> tx_pipes_;


}

#endif  // BESS_DRIVERS_ENSO_H