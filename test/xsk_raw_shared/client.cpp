#include "shared_definitions.hpp"


// create a client socket for ipc use
int create_ipc_socket() {
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	assert(fd >= 0);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

	int ret = connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
	assert(ret != -1);

	return fd;
}

static int hanlde_eth(pkt_context* ctx) {
	u16 proto = ntohs(ctx->eth->h_proto);
	switch(proto) {
		case ETH_P_IP:
			ctx->ip = reinterpret_cast<iphdr*>(ctx->eth + 1);
			std::cout << "IP packet\n";
			return 0;
		case ETH_P_IPV6:
			ctx->ipv6 = reinterpret_cast<ipv6hdr*>(ctx->eth + 1);
			std::cout << "IPV6 packet\n";
			return 0;
		default:
			printf("packet type: %d\n", proto);
			break;
	}
	return 0;
}


static void loop_receive(xdp_umem_shared* umem, xdp_sock* sock) {
	while(true) {
		xdp_desc desc;
		u32 entries = sock->rxr.rx_deq(&desc, 1);
		if(entries == 0) {
			continue;
		}
		pkt_context ctx;
		ctx.desc = &desc;
		ctx.pkt = umem->get_data(desc.addr);
		ctx.eth = reinterpret_cast<ethhdr*>(ctx.pkt);
		hanlde_eth(&ctx);
	}
}

int main(int argc, char* argv[]) {
	if(argc != 2) {
		printf("usage: client {interface_name}\n");
		return 0;
	}
	int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);


    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

	int client_fd = create_ipc_socket();
	
	xdp_program program;
	program.load("./kern.o", if_index);

	xdp_umem_shared* umem = new xdp_umem_shared();
	umem->map_shared_memory();
	umem->get_shared_fd(client_fd);

	xdp_sock* sock = new xdp_sock();
	int fd = socket(AF_XDP, SOCK_RAW, 0);
    assert(fd != -1);
	sock->fd = fd;
	sock->create();
	sock->bind_to_device_shared(if_index, umem->fd);

	program.update_map("xsks_map", 0, sock->fd);

	loop_receive(umem, sock);

	return 0;
}