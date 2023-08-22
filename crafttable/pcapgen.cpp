#include <arpa/inet.h>
#include <netinet/ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <pcap/pcap.h>
#include <sys/time.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

constexpr uint32_t kMaxPktSize = 1518;
constexpr char kDstMac[] = "aa:aa:aa:aa:aa:aa";
constexpr char kSrcMac[] = "bb:bb:bb:bb:bb:bb";

static constexpr uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (((uint32_t)a) << 24) | (((uint32_t)b) << 16) | (((uint32_t)c) << 8) |
         ((uint32_t)d);
}

int main(int argc, char const* argv[]) {
  if (argc != 6) {
    std::cerr << "Usage: " << argv[0] << " NB_PKTS PKT_SIZE NB_SRC NB_DST "
              << "OUTPUT_PCAP" << std::endl;
    exit(1);
  }

  const int total_nb_packets = std::stoi(argv[1]);
  const int pkt_size = std::stoi(argv[2]);
  const int nb_src = std::stoi(argv[3]);
  const int nb_dst = std::stoi(argv[4]);
  const std::string output_pcap = argv[5];

  // Skip if pcap with same name already exists.
  {
    std::ifstream f(output_pcap);
    if (f.good()) {
      std::cout << "Pcap with the same name already exists. Skipping."
                << std::endl;
      return 0;
    }
  }

  struct ether_addr dst_mac = *ether_aton(kDstMac);
  struct ether_addr src_mac = *ether_aton(kSrcMac);
  pcap_t* pd;
  pcap_dumper_t* pdumper;

  pd = pcap_open_dead(DLT_EN10MB, 65535);
  pdumper = pcap_dump_open(pd, output_pcap.c_str());
  struct timeval ts;
  ts.tv_sec = 0;
  ts.tv_usec = 0;
  uint8_t pkt[kMaxPktSize];
  memset(pkt, 0, kMaxPktSize);

  struct ether_header* l2_hdr = (struct ether_header*)&pkt;
  struct iphdr* l3_hdr = (struct iphdr*)(l2_hdr + 1);
  struct udphdr* l4_hdr = (struct udphdr*)(l3_hdr + 1);

  *((struct ether_addr*)&l2_hdr->ether_dhost) = dst_mac;
  *((struct ether_addr*)&l2_hdr->ether_shost) = src_mac;

  l2_hdr->ether_type = htons(ETHERTYPE_IP);
  l3_hdr->ihl = 5;
  l3_hdr->version = 4;
  l3_hdr->tos = 0;
  l3_hdr->id = 0;
  l3_hdr->frag_off = 0;
  l3_hdr->ttl = 255;
  l3_hdr->protocol = IPPROTO_UDP;

  struct pcap_pkthdr pkt_hdr;
  pkt_hdr.ts = ts;

  uint32_t src_ip = ip(192, 168, 0, 0);
  uint32_t dst_ip = ip(172, 16, 0, 0);

  uint32_t mss =
      pkt_size - sizeof(*l2_hdr) - sizeof(*l3_hdr) - sizeof(*l4_hdr) - 4;

  int nb_pkts = 0;

  while (nb_pkts < total_nb_packets) {
    for (int i = 0; i < nb_dst; ++i) {
      l3_hdr->daddr = htonl(dst_ip + (uint32_t)i);
      uint32_t src_offset = i / (nb_dst / nb_src);
      l3_hdr->saddr = htonl(src_ip + src_offset);

      l3_hdr->tot_len = htons(mss + sizeof(*l3_hdr) + sizeof(*l4_hdr));

      pkt_hdr.len = sizeof(*l2_hdr) + sizeof(*l3_hdr) + sizeof(*l4_hdr) + mss;
      pkt_hdr.caplen = pkt_hdr.len;

      l4_hdr->dest = htons(80);
      l4_hdr->source = htons(80);
      l4_hdr->len = htons(sizeof(*l4_hdr) + mss);

      ++(ts.tv_usec);
      pcap_dump((u_char*)pdumper, &pkt_hdr, pkt);

      ++nb_pkts;
      if (nb_pkts >= total_nb_packets) {
        break;
      }
    }
  }
  
   src_ip = ip(172, 16, 0, 0);
   dst_ip = ip(172, 15, 0, 0);

   mss =
      pkt_size - sizeof(*l2_hdr) - sizeof(*l3_hdr) - sizeof(*l4_hdr) - 4;

  
  nb_pkts = 0;

  while (nb_pkts < total_nb_packets) {
    for (int i = 0; i < nb_dst; ++i) {
      l3_hdr->daddr = htonl(dst_ip + (uint32_t)i);
      uint32_t src_offset = i / (nb_dst / nb_src);
      l3_hdr->saddr = htonl(src_ip + src_offset);

      l3_hdr->tot_len = htons(mss + sizeof(*l3_hdr) + sizeof(*l4_hdr));

      pkt_hdr.len = sizeof(*l2_hdr) + sizeof(*l3_hdr) + sizeof(*l4_hdr) + mss;
      pkt_hdr.caplen = pkt_hdr.len;

      l4_hdr->dest = htons(80);
      l4_hdr->source = htons(80);
      l4_hdr->len = htons(sizeof(*l4_hdr) + mss);

      ++(ts.tv_usec);
      pcap_dump((u_char*)pdumper, &pkt_hdr, pkt);

      ++nb_pkts;
      if (nb_pkts >= total_nb_packets) {
        break;
      }
    }
  }
  pcap_close(pd);
  pcap_dump_close(pdumper);

  return 0;
}
