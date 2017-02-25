#ifndef BRIDGE_H
#define BRIDGE_H

//port logical states
enum logical_state { ROOT, DESIGNATED, BLOCKED } typedef logical_state;
//port forwarding states
enum fwding_state { BLOCKED_FWD, LISTENING, LEARNING, FORWARDING } typedef fwding_state;

//bpdu structure
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

//packet containing a bpdu structure
typedef struct __attribute__((packed)) packet {
	struct ether_addr ether_dst;
	struct ether_addr ether_src;
	uint16_t length;
	uint8_t magic[3];
	bpdu bp;
} packet;

//structure of an entry in the forwarding table
typedef struct entry {
    struct ether_addr from;
    time_t age;
    int port;
} entry;

//structure for an extended bpdu
typedef struct ebpdu {
    bpdu bp;
    int port_num;
} ebpdu;

//structure for a port
typedef struct port {
    ebpdu vector;
    int sock;
    logical_state port_logical;
    fwding_state port_fwding;
    int timer;
} port;

#endif
