#include "../port.h"


// TODO: include Enso headers.




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
    
        // TODO: where to define class Arg?
        CommandResponse Init(const bess::pb::ENSOPortArg &arg);
        
        void DeInit() override;
        
        int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
        
        int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
    
    private:
        
        std::unique_ptr<Device> dev;
        std::vector<RxPipe*> rx_pipes;
        std::vector<TxPipe*> tx_pipes;
        // TODO: Enso basis.


}