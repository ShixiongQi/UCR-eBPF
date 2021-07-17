#include "shared_definitions.hpp"


// create a client socket for ipc use
int create_ipc_socket() {
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	assert(fd >= 0);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, DEFAULT::SOCKET_PATH, sizeof(addr.sun_path) - 1);

	int ret = connect(fd, (const struct sockaddr*)(&addr), sizeof(addr));
	assert(ret != -1);

	return fd;
}

static int handle_eth(pkt_context* ctx) {
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


static void loop_receive(xdp_umem_shared* umem, xdp_sock* sock, int client_fd) {
	while(true) {
		xdp_desc desc;
		u32 entries_available = sock->rxr.rx_nb_avail();
		if(entries_available == 0) {
			continue;
		}
		sock->rxr.rx_deq(&desc, 1);

		pkt_context ctx;
		ctx.desc = &desc;
		ctx.pkt = umem->get_data(desc.addr);
		ctx.eth = (ethhdr*)(ctx.pkt);
		handle_eth(&ctx);
		free_frame(desc.addr, client_fd);
	}
}

#pragma pack(push, 1)
struct test_udp {
	ethhdr eth;
	iphdr ip;
	udphdr udp;
	u8 content[10];
};
#pragma pack(pop)

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static inline unsigned short from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static unsigned int do_csum(const unsigned char *buff, int len)
{
	unsigned int result = 0;
	int odd;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long)buff;
	if (odd) {
#ifdef __LITTLE_ENDIAN
		result += (*buff << 8);
#else
		result = *buff;
#endif
		len--;
		buff++;
	}
	if (len >= 2) {
		if (2 & (unsigned long)buff) {
			result += *(unsigned short *)buff;
			len -= 2;
			buff += 2;
		}
		if (len >= 4) {
			const unsigned char *end = buff +
						   ((unsigned int)len & ~3);
			unsigned int carry = 0;

			do {
				unsigned int w = *(unsigned int *)buff;

				buff += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (buff < end);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *)buff;
			buff += 2;
		}
	}
	if (len & 1)
#ifdef __LITTLE_ENDIAN
		result += *buff;
#else
		result += (*buff << 8);
#endif
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

__sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *	This function code has been taken from
 *	Linux kernel lib/checksum.c
 */
__sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	return (__sum16)~do_csum((const unsigned char*)iph, ihl * 4);
}

// make a udp packet
// return the length of the packet
static int make_udp_packet(u8* start, int dest_index) {
	// 02:19:35:1b:ff:2f
	test_udp pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.eth.h_source[0] = 0x02;
	pkt.eth.h_source[1] = 0x19;
	pkt.eth.h_source[2] = 0x35;
	pkt.eth.h_source[3] = 0x1b;
	pkt.eth.h_source[4] = 0xff;
	pkt.eth.h_source[5] = 0x2f;

	memcpy(pkt.eth.h_dest, pkt.eth.h_source, 6);
	pkt.eth.h_proto = htons(ETH_P_IP);

	pkt.ip.version = 4;
	pkt.ip.ihl = 5;
	pkt.ip.tot_len = sizeof(test_udp) - sizeof(ethhdr);
	pkt.ip.ttl = 255;
	pkt.ip.protocol = IPPROTO_UDP;
	pkt.ip.saddr = inet_addr("10.11.1.1");
	pkt.ip.daddr = inet_addr("10.11.1.1");
	pkt.ip.check = ip_fast_csum(&pkt.ip, pkt.ip.ihl);

	*((int*)&pkt.udp) = dest_index;

	memcpy(start, &pkt, sizeof(pkt));
	return sizeof(pkt);
}

static void loop_send(xdp_umem_shared* umem, xdp_sock* sock, int dest_index, int client_fd) {
	while(true) {
		sleep(1);

		printf("send packet to map index %d\n", dest_index);
		xdp_desc desc;
		u32 entries = sock->txr.tx_nb_free();
		if(entries == 0) {
			continue;
		}

		// generate packet
		u64 frame = alloc_frame(client_fd);
		desc.addr = frame;
		u8* pkt_start = umem->get_data(desc.addr);
		desc.len = make_udp_packet(pkt_start, dest_index);
		sock->txr.tx_enq(&desc, 1);
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

	int client_fd = create_ipc_socket();
	
	xdp_program program;
	program.get_shared(client_fd);

	xdp_umem_shared* umem = new xdp_umem_shared();
	umem->map_shared_memory();
	umem->get_shared_fd(client_fd);

	xdp_sock* sock = new xdp_sock();
	int fd = socket(AF_XDP, SOCK_RAW, 0);
    assert(fd != -1);
	sock->fd = fd;
	sock->create();
	sock->bind_to_device_shared(if_index, umem->fd);

	int bpf_map_index = getpid() % DEFAULT::BPF_MAP_MAX_ENTRIES;
	printf("set xdp socket at map index %d\n", bpf_map_index);
	program.update_map(bpf_map_index, sock->fd);

	if(strcmp(argv[2], "send") == 0) {
		assert(argc >= 4);

		loop_send(umem, sock, atoi(argv[3]), client_fd);
	}else if(strcmp(argv[2], "recv") == 0) {
		loop_receive(umem, sock, client_fd);
	}

	return 0;
}