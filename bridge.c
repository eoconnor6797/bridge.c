#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/ether.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include "bridge.h"

//global mutex to prevent data races
pthread_mutex_t mutex;
// change variable name / find&replace
int table_len;

//Packet to be sent
packet my_packet;
//my MAC address
struct ether_addr me;
//my ebpdu
ebpdu my_bpdu;

//No bridge bpdu
bpdu no_bridge_bpdu = { 0 };
ebpdu no_bridge;

//the bpdu to be sent out (kinda)
ebpdu bridge_bpdu;

//the port connected to root
int root_port = -1;

//the mac address that signifies a bdpu
struct ether_addr bpdu_dst;

//forwarding table
entry table[300] = { 0 };
//array of ports
port* ports;

//Timer that changes forwarding state
void* forward_state_timer(void* arg) {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < table_len; i++) {
            //if state should be switched
            if (ports[i].timer == 1) {
                if (ports[i].port_fwding == LISTENING) {
                    ports[i].timer = 15;
                    ports[i].port_fwding = LEARNING;
                 } else if (ports[i].port_fwding == LEARNING) {
                    ports[i].port_fwding = FORWARDING;
                    ports[i].timer = 0;
                }
            //if timer should be decremented
            } else if (ports[i].port_fwding != FORWARDING) {
                ports[i].timer--;
            }
        }
        pthread_mutex_unlock(&mutex);
    }
}

//bpdu timeout timer
void* bpdu_timeout(void* arg) {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&mutex);
        //for ports in array
        for (int i = 0; i < table_len; i++) {
            if (ports[i].vector.bp.stp_msg_age) {
                //increment age
                ports[i].vector.bp.stp_msg_age += htons(256);
                //if age is greater than timeout value
                if (ntohs(ports[i].vector.bp.stp_msg_age) > 5120) {
                    ports[i].vector = no_bridge;
                }
            }
        }
        //for bridge_bpdu
        if (bridge_bpdu.bp.stp_msg_age) {
            //increment age
            bridge_bpdu.bp.stp_msg_age += htons(256);
            //check for timeout
            if (ntohs(bridge_bpdu.bp.stp_msg_age) > 5120) {
                bridge_bpdu = my_bpdu;
                root_port = -1;
            }
        }
        pthread_mutex_unlock(&mutex);

    }
}

//timer for transmitting bpdu
void* transmit_timer(void* arg) {
    while (1) {
        sleep(2);
        pthread_mutex_lock(&mutex);
        ebpdu tmp = bridge_bpdu;
        //set values
        tmp.bp.stp_bridge_pri = htons(0x8000);
        tmp.bp.stp_bridge_mac = me;
        my_packet.bp = tmp.bp;
        //for port in array
        for (int i = 0; i < table_len; i++) {
            //set port id
            my_packet.bp.stp_port_id = htons(i);
            //send the bpdu!
            send(ports[i].sock, &my_packet, sizeof(my_packet), 0);
        }
        pthread_mutex_unlock(&mutex);
    }
}

//check if two bpdus are the same
int same_bpdu(ebpdu xx, ebpdu yy) {
    return (xx.port_num == yy.port_num) &&
        (xx.bp.stp_root_pri == yy.bp.stp_root_pri) &&
        (memcmp(&(xx.bp.stp_root_mac), &(yy.bp.stp_root_mac), 6) == 0) &&
        (xx.bp.stp_root_cost == yy.bp.stp_root_cost) &&
        (xx.bp.stp_bridge_pri == yy.bp.stp_bridge_pri) &&
        (memcmp(&(xx.bp.stp_bridge_mac), &(yy.bp.stp_bridge_mac), 6) == 0) &&
        (xx.bp.stp_port_id == yy.bp.stp_port_id);
}

//on bpdu reception
void packet_rec(ebpdu p, int i) {
    ports[i].vector = p;
    ports[i].vector.bp.stp_msg_age += htons(256);
    ports[i].vector.bp.stp_port_id = i;
    if (same_bpdu(ports[i].vector, my_bpdu)) {
        bridge_bpdu.bp.stp_msg_age = ports[i].vector.bp.stp_msg_age;
    }
}

//check if given id values are better than the other id
int better_id(struct ether_addr xx, int xx_pri, struct ether_addr yy, int yy_pri) {
    if (xx_pri < yy_pri || (xx_pri == yy_pri && memcmp(&xx, &yy, 6) < 0)) {
        return 1;
    } else if (xx_pri == yy_pri && memcmp(&xx, &yy, 6) == 0) {
        return 0;
    } else {
        return -1;
    }

}

//Check if a bpdu is better
int better_bpdu(ebpdu best, ebpdu ii) {
    int root_cmp = better_id(best.bp.stp_root_mac, best.bp.stp_root_pri,
            ii.bp.stp_root_mac, ii.bp.stp_root_pri);
    //if root_cmp is not the same
    if (root_cmp) {
        return root_cmp == 1;
        //if they are the same
    } else {
        //if cost is the same
        if (best.bp.stp_root_cost == ii.bp.stp_root_cost) {
            int bridge_cmp = better_id(best.bp.stp_bridge_mac, best.bp.stp_bridge_pri,
                    ii.bp.stp_bridge_mac, ii.bp.stp_bridge_pri);
            //if bridge_cmp is not the same
            if (bridge_cmp) {
                return bridge_cmp == 1;
            } else {
                //compare port_id
                return best.bp.stp_port_id < ii.bp.stp_port_id;
            }
        } else {
            //compare root_cost
            return best.bp.stp_root_cost < ii.bp.stp_root_cost;
        }
    }
}

//find best bpdu in array of ports
int best() {
    int best = 0;
    for (int ii = 0; ii < table_len; ii++) {
        if (better_bpdu(ports[best].vector, ports[ii].vector)) {
        } else {
            best = ii;
        }
    }
    return best;
}

//update the spanning tree
void stp_update() {
    //get best bpdu from ports
    ebpdu bv = ports[best()].vector;
    //if same return
    if (same_bpdu(bv, bridge_bpdu)) {
        return;
    }
    //if better update bridge_bpdu
    if (better_bpdu(bv, bridge_bpdu)) {
        bridge_bpdu = bv;
        bridge_bpdu.bp.stp_bridge_mac = me;
        bridge_bpdu.bp.stp_root_cost += htonl(10);
        root_port = bv.bp.stp_port_id;
    }
    //update root port
    if (ports[root_port].port_logical != ROOT) {
        ports[root_port].port_logical = ROOT;
        ports[root_port].port_fwding = LISTENING;
        ports[root_port].timer = 15;
    }
    //update ports in port table
    for (int ii = 0; ii < table_len; ii++) {
        if (ii != root_port) {
            if (better_bpdu(bridge_bpdu, ports[ii].vector)) {
                if (ports[ii].port_logical != DESIGNATED) {
                    ports[ii].port_logical = DESIGNATED;
                    ports[ii].port_fwding = LISTENING;
                    ports[ii].timer = 15;
                }
            } else {
                if (ports[ii].port_logical != BLOCKED) {
                    ports[ii].port_logical = BLOCKED;
                    ports[ii].port_fwding = BLOCKED_FWD;
                    ports[ii].timer = 0;
                }
            }
        }
    }
}

/* all bytes in AF_UNIX abstact socket names are significant, even 0s,
 * so we need this to be compatible with the python code in 'wires'
 */
int addrlen(struct sockaddr_un *a)
{
    return sizeof(a->sun_family) + 1 + strlen(&a->sun_path[1]);
}

//function that listens on each port
void* listen_port(void* arg) {
    //Get index number
    int index = (int) arg;
    //get socket fd
    int sock = ports[index].sock;

    unsigned char buf[1500] = { 0 };    /* unsigned so printf %02x works... */
    while (1) {
        size_t rv = recv(sock, buf, sizeof(buf), 0);

        struct ether_addr *dst = (struct ether_addr*)buf; /* very crude unpacking */
        struct ether_addr *src = (struct ether_addr*)&buf[6];

        //lock accessing array again
        pthread_mutex_lock(&mutex);
        //if a bpdu was sent
        if (memcmp(dst, &bpdu_dst, sizeof(struct ether_addr)) == 0) {
            //create ebpdu from recieved packet
            ebpdu tmp;
            tmp.bp = ((packet*) buf)->bp;
            tmp.port_num = index;
            //call packet_rec
            packet_rec(tmp, index);
        }
        //if port is in correct forwarding state
        if (ports[index].port_fwding == LEARNING || ports[index].port_fwding == FORWARDING) {
            //store src in forwarding table
            int exists = 0;
            for (int i = 0; i < 300; i++) {
                //if entry exists update it
                if (memcmp(&(table[i].from), src, 6) == 0) {
                    table[i].age = time(NULL);
                    table[i].port = index;
                    exists = 1;
                    break;
                }
            }
            //if entry does not exist create it
            if (!exists) {
                for (int i = 0; i < 300; i++) {
                    if (memcmp(&(table[i].from), "\0\0\0\0\0\0", 6) == 0) {
                        memcpy(&(table[i].from), src, sizeof(struct ether_addr));
                        table[i].age = time(NULL);
                        table[i].port = index;
                        break;
                    }
                }
            }

            //if port is forwarding state and packet is not a bpdu
            if (ports[index].port_fwding == FORWARDING && memcmp(dst, &bpdu_dst, sizeof(struct ether_addr)) != 0) {
                //Check if the destination is known
                int known = -1;
                for (int i = 0; i < 300; i++) {
                    if (memcmp(&(table[i].from), dst, 6) == 0) {
                        known = i;
                    }
                }

                //If broadcast packet or unknown destination send to all
                if (buf[0] & 0x01 || known == -1) {
                    for (int j = 0; j < table_len; j++) {
                        //check if port is forwarding and not the receiving port
                        if (j != index && ports[j].port_fwding == FORWARDING) {
                            send(ports[j].sock, buf, sizeof(unsigned char) * rv, 0);
                        }
                    }
                    //if destination is known and port is in forwarding state
                } else if (ports[table[known].port].port_fwding == FORWARDING){
                    send(ports[table[known].port].sock, buf, sizeof(unsigned char) * rv, 0);
                }
            }
        }
        //update spanning tree after every reception
        stp_update();
        //unlock done
        pthread_mutex_unlock(&mutex);

    }
    //Should never be reached
    return 0;
}

//initialize global variable
void initialize(int argc, char* argv[]) {
    //Get amount of ports
    table_len = argc - 2;

    //get the destination of bpdu packets
    bpdu_dst = *ether_aton("01:80:c2:00:00:00");

    //Create space for the forwarding table
    ports = malloc(sizeof(port) * table_len);

    //create me
    me = *ether_aton(argv[1]);

    //crete my_bpdu
    my_bpdu.bp.stp_root_pri = htons(0x8000);
    my_bpdu.bp.stp_root_mac = me;
    my_bpdu.bp.stp_root_cost = 0;
    my_bpdu.bp.stp_bridge_pri = htons(0x8000);
    my_bpdu.bp.stp_bridge_mac = me;
    my_bpdu.bp.stp_msg_age = 0;
    my_bpdu.bp.max_age = htons(20 * 256);
    my_bpdu.bp.hello_time = htons(512);
    my_bpdu.bp.forward_delay = htons(15 * 256);
    my_bpdu.port_num = -1;

    //Create no_bridge
    no_bridge_bpdu.stp_root_pri = 0xFFFF;
    no_bridge.bp = no_bridge_bpdu;
    no_bridge.port_num = -1;

    //create bridge_bpdu
    bridge_bpdu = my_bpdu;

    //create my_packet
    my_packet.ether_dst = bpdu_dst;
    my_packet.ether_src = me;
    my_packet.length = htons(38);
    my_packet.magic[0] = 0x42;
    my_packet.magic[1] = 0x42;
    my_packet.magic[2] = 0x03;

    //Initialize the array of ports
    for (int i = 0; i < table_len; i++) {
        port p;
        //set port values
        p.vector = no_bridge;
        p.sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        p.port_fwding = LISTENING;
        p.port_logical = DESIGNATED;
        p.timer = 15;

        ports[i] = p;

        //Black magic
        struct sockaddr_un l_unix = {.sun_family = AF_UNIX, .sun_path = {0,}};
        sprintf(&l_unix.sun_path[1], "%s.host-%s (wire %d)", getenv("USER"), argv[1], i);
        bind(ports[i].sock, (struct sockaddr*)&l_unix, addrlen(&l_unix));

        struct sockaddr_un w_unix = {.sun_family = AF_UNIX, .sun_path = {0,}};
        sprintf(&w_unix.sun_path[1], "%s.wire.%d", getenv("USER"), i);
        if (connect(ports[i].sock, (struct sockaddr*)&w_unix, addrlen(&w_unix)) < 0)
            perror("can't connect");
    }
}

int main(int argc, char* argv[])
{
    //initialize global variables
    initialize(argc, argv);

    pthread_t state;
    pthread_t timeout;
    pthread_t transmit;
    pthread_create(&state, NULL, forward_state_timer, NULL);
    pthread_create(&timeout, NULL, bpdu_timeout, NULL);
    pthread_create(&transmit, NULL, transmit_timer, NULL);

    //Create a thread for each port
    pthread_t threads[table_len];
    for (int i = 0; i < table_len; i++) {
        pthread_create(&threads[i], NULL, listen_port, (void*)i);
    }

    //Don't exit main thread
    while (1) {
        usleep(500000);
        pthread_mutex_lock(&mutex);
        //if age older than 15s delete from table
        for (int i = 0; i < 300; i++) {
            if (time(NULL) - table[i].age >= 15) {
                memset(&(table[i].from), 0, sizeof(struct ether_addr));
            }
        }
        //unlock
        pthread_mutex_unlock(&mutex);
    }
}
