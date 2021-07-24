#include "shared_definitions.h"


static void free_frame(u64 frame, int client_fd) {
	u8 buffer[DEFAULT::SOCKET_MAX_BUFFER_SIZE];
	*((u32*)buffer) = (u32)RequestType::DEALLOC;
	*((u64*)(buffer + 4)) = frame;
	int ret = send(client_fd, buffer, 12, MSG_EOR);
	assert(ret != -1);
}

static u64 alloc_frame(int client_fd) {
	u8 buffer[DEFAULT::SOCKET_MAX_BUFFER_SIZE];
	*((u32*)buffer) = (u32)RequestType::ALLOC;
	int ret = send(client_fd, buffer, 4, MSG_EOR);
	assert(ret != -1);
	ret = recv(client_fd, buffer, sizeof(buffer), 0);
	assert(ret != -1);
	assert(ret != 0);

	return *((u64*)buffer);
}


static void loop_receive(xdp_umem* umem, xdp_sock* sock, int client_fd) {
	while(true) {
		xdp_desc desc;
		u32 entries_available = sock->rxr.nb_avail();
		if(entries_available == 0) {
			continue;
		}
		sock->rxr.deq(&desc, 1);

		u8* pkt = umem->get_data(desc.addr);
		int dest_index = *((int*)pkt);
		u64 frame = *((u64*)(pkt + 4));
		printf("receive packet: dest_index: %d   frame: %lu   time: %ld\n", dest_index, frame, xdp_time::get_time_nano());

		free_frame(desc.addr, client_fd);
	}
}


#pragma pack(push, 1)
struct test_pkt {
	//ethhdr eth;
	//iphdr ip;
	//udphdr udp;
	int dest_index;
	u64 frame;
};
#pragma pack(pop)


// make a packet
// int(dest_index) u64(frame)
// return the length of the packet
static int make_packet(u8* start, int dest_index, u64 frame) {
	test_pkt* pkt = (test_pkt*)start;

	// // b2:c5:15:65:fa:ef
	// pkt->eth.h_source[0] = 0xb2;
	// pkt->eth.h_source[1] = 0xc5;
	// pkt->eth.h_source[2] = 0x15;
	// pkt->eth.h_source[3] = 0x65;
	// pkt->eth.h_source[4] = 0xfa;
	// pkt->eth.h_source[5] = 0xef;

	// // 72:68:5e:62:17:ed
	// pkt->eth.h_dest[0] = 0x72;
	// pkt->eth.h_dest[1] = 0x68;
	// pkt->eth.h_dest[2] = 0x5e;
	// pkt->eth.h_dest[3] = 0x62;
	// pkt->eth.h_dest[4] = 0x17;
	// pkt->eth.h_dest[5] = 0xed;

	//pkt->eth.h_proto = htons(ETH_P_IP);

	pkt->dest_index = dest_index;
	pkt->frame = frame;

	return sizeof(test_pkt);
}

static void loop_send(xdp_umem* umem, xdp_sock* xdp, int dest_index, int client_fd) {
	while(true) {
		sleep(1);

		printf("send packet to map index %d\n", dest_index);
		
		u32 entries = xdp->txr.nb_free();
		if(entries == 0) {
			continue;
		}

		// generate packet
		u64 frame = alloc_frame(client_fd);
		u8* pkt_start = umem->get_data(frame);

        xdp_desc desc;
        desc.addr = frame;
		desc.len = make_packet(pkt_start, dest_index, frame);
		xdp->txr.enq(&desc, 1);

		printf("send packet: dest_index: %d   frame: %lu  time: %ld\n", dest_index, frame, xdp_time::get_time_nano());
		sendto(xdp->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
	}
}


int main(int argc, char* argv[]) {
	if(argc < 3) {
		printf("usage: client {interface_name} {send/recv} [bpf_key]\n");
		return 0;
	}
	int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);


    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    Socket sock;
    sock.create_client_socket();
	
	xdp_program program;
	program.get_shared(sock);

	xdp_umem* umem = new xdp_umem();
	umem->map_shared_memory();
	umem->get_shared_fd(sock);

	xdp_sock* xdp = new xdp_sock();
	xdp->fd = socket(AF_XDP, SOCK_RAW, 0);
    assert(xdp->fd != -1);
	xdp->create();
	xdp->bind_to_device_shared(if_index, umem->fd);

	int bpf_map_index = getpid() % DEFAULT::BPF_MAP_MAX_ENTRIES;
	printf("set xdp socket at map index %d\n", bpf_map_index);
	program.update_map(bpf_map_index, xdp->fd);

	if(strcmp(argv[2], "send") == 0) {
		assert(argc >= 4);

		loop_send(umem, xdp, atoi(argv[3]), sock);
	}else if(strcmp(argv[2], "recv") == 0) {
		loop_receive(umem, xdp, sock);
	}

	return 0;
}