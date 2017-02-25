#ifndef BPDU_H
#define BPDU_H

typedef struct __attribute__((packed)) bpdu {
	uint16_t protocol;
	uint8_t version;
	uint8_t type;
	uint8_t flags;
	uint16_t stp_root_pri;
	struct ether_addr stp_root_mac;
	uint32_t stp_root_cost;
	uint16_t stp_bridge_pri;
	struct ether_addr stp_bridge_mac;
	uint16_t stp_port_id;
	uint16_t stp_msg_age;
	uint16_t max_age;
	uint16_t hello_time;
	uint16_t forward_delay;
	uint8_t pad[8];
} bpdu;

typedef struct __attribute__((packed)) packet {
	struct ether_addr ether_dst;
	struct ether_addr ether_src;
	uint16_t length;
	uint8_t magic[3];
	bpdu bp;
} packet;

int decode(void);

uint32_t parseNum(char* buf, char* find, int size);

struct ether_addr* parseMAC(char* buf, char* find);

void encode(void);

#endif
