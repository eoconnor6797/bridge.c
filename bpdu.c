#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/ether.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include "bpdu.h"

uint8_t buf[500];

int decode() {
	packet* p = (packet*) buf;

	if ((ntohs(p->length)) != 38) {
		return -1;
	}

	if (p->magic[0] != 0x42 || p->magic[1] != 0x42 || p->magic[2] != 0x03) {
		return -1;
	}

	bpdu b = p->bp;

	if (b.protocol != 0 || b.version != 0 || b.type != 0) {
		return -1;
	}

	printf("ether_dst %s\n", ether_ntoa(&(p->ether_dst)));

	printf("ether_src %s\n", ether_ntoa(&(p->ether_src)));

	printf("stp_root_pri %hu\n", ntohs(b.stp_root_pri));

	printf("stp_root_mac %s\n", ether_ntoa(&(b.stp_root_mac)));

	printf("stp_root_cost %u\n", ntohl(b.stp_root_cost));
	printf("stp_bridge_pri %hu\n", ntohs(b.stp_bridge_pri));

	printf("stp_bridge_mac %s\n", ether_ntoa(&(b.stp_bridge_mac)));

	printf("stp_port_id %hu\n", ntohs(b.stp_port_id));
	printf("stp_msg_age %hu\n", ntohs(b.stp_msg_age));

	return 0;
}

uint32_t parseNum(char* buf, char* find, int size) {
	size_t len = strlen(find);
	char* start = strstr(buf, find);
	if (size == sizeof(uint16_t)) {
		return htons(atoi(start + len));
	} else {
		return htonl(atoi(start + len));
	}
}

struct ether_addr* parseMAC(char* buf, char* find) {
	size_t len = strlen(find);
	char* start = strstr(buf, find);
	return ether_aton(start + len);
}

void encode() {
	packet p = { 0 };
	char* text = (char*) buf;

	p.ether_dst = *parseMAC(text, "ether_dst ");
	p.ether_src = *parseMAC(text, "ether_src ");

	p.length = htons(38);
	p.magic[0] = 0x42;
	p.magic[1] = 0x42;
	p.magic[2] = 0x03;
	p.bp.stp_root_pri = (uint16_t) parseNum(text, "stp_root_pri ", sizeof(uint16_t));

	p.bp.stp_root_mac = *parseMAC(text, "stp_root_mac ");

	p.bp.stp_root_cost = parseNum(text, "stp_root_cost ", sizeof(uint32_t));
	p.bp.stp_bridge_pri = (uint16_t) parseNum(text, "stp_bridge_pri ", sizeof(uint16_t));

	p.bp.stp_bridge_mac = *parseMAC(text, "stp_bridge_mac ");

	p.bp.stp_port_id = (uint16_t) parseNum(text, "stp_port_id ", sizeof(uint16_t));
	p.bp.stp_msg_age = (uint16_t) parseNum(text, "stp_msg_age ", sizeof(uint16_t));
	p.bp.max_age = htons(5120);
	p.bp.hello_time = htons(512);
	p.bp.forward_delay = htons(3840);

	write(1, &p, sizeof(packet));
}

//CLEAN THIS UP
int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("Please specify encode or decode and pipe input to stdin\n");
		exit(1);
	}

	//CLEAN
	read(0, buf, 500);
	if (strcmp(argv[1], "decode") == 0) {
		if (decode() == -1) {
			printf("ERROR\n");
			exit(1);
		}
	} else if (strcmp(argv[1], "encode") == 0) {
		encode();
	} else {
		printf("Only valid arguments are encode or decode\n");
	}
}

