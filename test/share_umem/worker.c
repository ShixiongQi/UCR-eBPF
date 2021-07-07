#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_xdp.h>
#include <stddef.h>
#include <sys/socket.h>
#include <linux/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <bpf/bpf.h>
#include <bpf/xsk.h>
#include <bpf/libbpf.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <errno.h>
#include <time.h>
#include "extra_definitions.h"

struct xsk_ring_stats {
	u64 rx_npkts;
	u64 tx_npkts;
    u32 outstanding_rx;
};


struct xsk_driver_stats {
	u64 intrs;
	u64 prev_intrs;
};

// information about an umem, including fill and completion ring and buffer pointer
struct xsk_umem_info {
    // the fill ring
	struct xsk_ring_prod fq;
    // the completion ring
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};


struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
    // The UMEM uses two rings: FILL and COMPLETION.
    // Each socket associated with the UMEM must have an RX queue, TX queue or both.
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	struct xsk_ring_stats ring_stats;
	struct xsk_driver_stats drv_stats;
};

struct pkt_context {
	const struct xdp_desc* desc;
	u8* pkt;
	struct ethhdr* eth;
	struct iphdr* ip;
	struct ipv6hdr* ipv6;
	struct udphdr* udp;
};

struct config {
	int queue_id;
	u32 batch_size;
	u32 xdp_flags;
	const char* device_name;
	const char* xdp_program_path;
	int opt_send;
};

static struct config cfg;
static int client_fd;
static int should_exit = 0;
static struct xsk_socket_info xsk;

static void int_exit(int sig) {
	should_exit = 1;
}

static struct config get_default_config() {
	struct config cfg = {
		.queue_id = 0,
		.batch_size = 64,
		.xdp_flags = XDP_FLAGS_DRV_MODE,
		.xdp_program_path = "./kern.o",
		.opt_send = 0
	};

	return cfg;
}


static void get_monotonic_time(struct timespec* ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static long get_time_nano(struct timespec* ts) {
    return (long)ts->tv_sec * 1e9 + ts->tv_nsec;
}

static double get_elapsed_time_sec(struct timespec* before, struct timespec* after) {
    double deltat_s  = after->tv_sec - before->tv_sec;
    double deltat_ns = after->tv_nsec - before->tv_nsec;
    return deltat_s + deltat_ns*1e-9;
}

static long get_elapsed_time_nano(struct timespec* before, struct timespec* after) {
    return get_time_nano(after) - get_time_nano(before);
}

// allocate a umem frame, return the descriptor
static u64 alloc_frame() {
    u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];

    *((u32*)buffer) = REQUEST_ALLOC;
    int ret = send(client_fd, buffer, sizeof(u32), MSG_EOR);
    if(ret == -1) {
        exit_with_error("send failed");
    }

    ret = recv(client_fd, buffer, sizeof(buffer), MSG_WAITALL);
    if(ret == -1) {
        exit_with_error("recvmsg failed");
    }else if(ret == 0) {
        exit_with_error("no data to read");
    }

    u64 frame = *((u64*)buffer);
    return frame;
}

// free a umem frame.
static void free_frame(u64 frame) {
	u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];

    *((u32*)buffer) = REQUEST_DEALLOC;
    *((u64*)(buffer + 4)) = frame;
    int ret = send(client_fd, buffer, sizeof(u32) + sizeof(u64), MSG_EOR);
    if(ret == -1) {
        exit_with_error("send failed");
    }
}

// allow unlimited locking of memory
static void allow_unlimited_locking() {
	struct rlimit lim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};

	int ret = setrlimit(RLIMIT_MEMLOCK, &lim);
	if(ret != 0) {
		exit_with_error("set limit failed");
	}
}

static struct xsk_umem_info* create_umem() {
    struct xsk_umem_info *umem = calloc(1, sizeof(*umem));
    if(!umem) {
        exit_with_error("memory allocation failed");
    }

    int fd = shm_open(DEFAULT_UMEM_FILE_NAME, O_RDWR, 0);
    if(fd < 0) {
        exit_with_error("shm_open failed");
    }

    umem->buffer = mmap(NULL, DEFAULT_UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(umem->buffer == MAP_FAILED) {
        exit_with_error("mmap failed");
    }

    struct xsk_umem_config umem_cfg = {
        .fill_size = DEFAULT_CLIENT_FRAMES,
        .comp_size = DEFAULT_CLIENT_FRAMES,
        .frame_size = DEFAULT_FRAME_SIZE,
        .frame_headroom = DEFAULT_HEADROOM,
        .flags = DEFAULT_UMEM_FLAGS
    };

    int ret = xsk_umem__create(&umem->umem, umem->buffer, DEFAULT_UMEM_SIZE, &umem->fq, &umem->cq, &umem_cfg);
    if(ret != 0) {
        exit_with_error("xsk_umem__create failed");
    }

    // populate the fill ring of an umem
	// idx is the cached_prod before reserving
    u32 idx;
    ret = xsk_ring_prod__reserve(&umem->fq, DEFAULT_CLIENT_FRAMES, &idx);
    if(ret != DEFAULT_CLIENT_FRAMES) {
        exit_with_error("xsk_ring_prod__reserve failed");
    }

    for(u32 i=0; i<DEFAULT_CLIENT_FRAMES; i++) {
        *xsk_ring_prod__fill_addr(&umem->fq, idx++) = alloc_frame();
    }
    xsk_ring_prod__submit(&umem->fq, DEFAULT_NUM_FRAMES);

    return umem;
}


// create a af_xdp socket
static void create_socket(struct xsk_umem_info* umem) {
	struct xsk_socket_config socket_cfg;
	memset(&xsk, 0, sizeof(xsk));

	xsk.umem = umem;
	socket_cfg.rx_size = DEFAULT_CLIENT_FRAMES;
	socket_cfg.tx_size = DEFAULT_CLIENT_FRAMES;
	// // We need to supply XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD to load our own XDP programs
	socket_cfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD ;
	socket_cfg.xdp_flags = cfg.xdp_flags;
	// The kernel will first try to use zero-copy copy.
	// If zero-copy is not supported, it will fall back on using copy mode
	// Of course, we can force a certain mode if we want to.
	socket_cfg.bind_flags = 0; //XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;

	int ret = xsk_socket__create(&xsk.xsk, cfg.device_name, cfg.queue_id, xsk.umem->umem, &xsk.rx, &xsk.tx, &socket_cfg);
	
	if(ret) {
		exit_with_error("xsk_socket__create failed");
	}

}


// set up busy poll options for socket
static void setup_socket_options() {
	int ret;
	int sock_opt;
	int sock_fd = xsk_socket__fd(xsk.xsk);
	
	sock_opt = 1;
	ret = setsockopt(sock_fd, SOL_SOCKET, SO_PREFER_BUSY_POLL, (void*)&sock_opt, sizeof(sock_opt));
	if(ret < 0) {
		exit_with_error("setsockopt failed");
	}

	sock_opt = 20;
	ret = setsockopt(sock_fd, SOL_SOCKET, SO_BUSY_POLL, (void*)&sock_opt, sizeof(sock_opt));
	if(ret < 0) {
		exit_with_error("setsockopt failed");
	}

	sock_opt = cfg.batch_size;
	ret = setsockopt(sock_fd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, (void*)&sock_opt, sizeof(sock_opt));
	if(ret < 0) {
		exit_with_error("setsockopt failed");
	}
	
}

// free the descriptor in complete ring to reclaim space
static void clear_complete_ring() {
    u32 idx;
    u32 num_sent = xsk_ring_cons__peek(&xsk.umem->cq, DEFAULT_CLIENT_FRAMES, &idx);
    if(num_sent > 0) {
		// printf("clear complete packet %d\n", num_sent);
		for(u32 i=0; i<num_sent; i++) {
			free_frame(*xsk_ring_cons__comp_addr(&xsk.umem->cq, idx++));
		}
        xsk_ring_cons__release(&xsk.umem->cq, num_sent);
        xsk.ring_stats.outstanding_rx -= num_sent;
    }
}

static void tx_send(u64 addr, u32 len) {
    // free the space from last sent
    clear_complete_ring();

    u32 idx;
    int ret = xsk_ring_prod__reserve(&xsk.tx, 1, &idx);
    if(ret != 1) {
        exit_with_error("xsk_ring_prod__reserve failed");
    }

    struct xdp_desc* desc = xsk_ring_prod__tx_desc(&xsk.tx, idx);
    desc->addr = addr;
    desc->len = len;
    xsk.ring_stats.outstanding_rx++;
    xsk_ring_prod__submit(&xsk.tx, 1);
}

// return 0 if the space of packet need to be freed
static int handle_udp_packet(struct pkt_context* ctx) {
    // printf("UDP\n");
	if(cfg.opt_send == 0) {
		return 0;
	}
	// swap MAC
	u8 tmp_mac[ETH_ALEN];
	memcpy(tmp_mac, ctx->eth->h_dest, ETH_ALEN);
	memcpy(ctx->eth->h_dest, ctx->eth->h_source, ETH_ALEN);
	memcpy(ctx->eth->h_source, tmp_mac, ETH_ALEN);
	
	// swap IP
	struct in6_addr tmp_ip;
	memcpy(&tmp_ip, &ctx->ipv6->saddr, sizeof(tmp_ip));
	memcpy(&ctx->ipv6->saddr, &ctx->ipv6->daddr, sizeof(tmp_ip));
	memcpy(&ctx->ipv6->daddr, &tmp_ip, sizeof(tmp_ip));
	// int ret = inet_pton(AF_INET6, "fc00:dead:cafe:1::2", &ctx->ipv6->daddr);
	// if(ret != 1) {
	// 	exit_with_error("inet_pton failed");
	// }
	struct timespec ts;
	get_monotonic_time(&ts);
	long timestamp = get_time_nano(&ts);
	printf("send %ld\n", timestamp);
	tx_send(ctx->desc->addr, ctx->desc->len);
	// if in copy mode, tx is driven by a syscall
	ssize_t ret = sendto(xsk_socket__fd(xsk.xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
	// printf("sendto returns %ld errno is %d\n", ret, errno);

	return 1;
}

// return 0 if the space of packet need to be freed
static int handle_ip_packet(struct pkt_context* ctx) {
	// printf("IP\n");
    return 0;
}

//// return 0 if the space of packet need to be freed
static int handle_ipv6_packet(struct pkt_context* ctx) {
	// printf("IPV6\n");
    u8 protocol = ctx->ipv6->nexthdr;
    switch(protocol) {
        case IPPROTO_UDP:
			ctx->udp = (struct udphdr*)(ctx->ipv6 + 1);
            return handle_udp_packet(ctx);
        default:
            break;
    }
	return 0;
}

// the logic to process the packet
// return 0 if the space of packet need to be freed
static int handle_packet(const struct xdp_desc* desc) {
	struct pkt_context ctx;
	ctx.desc = desc;
	ctx.pkt = xsk_umem__get_data(xsk.umem->buffer, desc->addr);
	//printf("addr: %p length: %d\n", ctx.pkt, desc->len);

	ctx.eth = (struct ethhdr*)ctx.pkt;
	u16 proto = ntohs(ctx.eth->h_proto);
	switch (proto)
	{
		case ETH_P_IP:
			ctx.ip = (struct iphdr*)(ctx.eth + 1);
			return handle_ip_packet(&ctx);
		case ETH_P_IPV6:
			ctx.ipv6 = (struct ipv6hdr*)(ctx.eth + 1);
			return handle_ipv6_packet(&ctx);
		default:
			printf("packet type: %d\n", proto);
			break;
	}
	return 0;
}

// fill the fill ring as much as possible
static void fill_fill_ring() {
    u32 fq_index;
    u32 free_cnt = xsk_prod_nb_free(&xsk.umem->fq, DEFAULT_CLIENT_FRAMES);
    if(free_cnt > 0) {
        // This should not happen, but check it just in case
        if(xsk_ring_prod__reserve(&xsk.umem->fq, free_cnt, &fq_index) != free_cnt) {
            exit_with_error("cannot reserve frames");
        }

        for(u32 i=0; i<free_cnt; i++) {
            *xsk_ring_prod__fill_addr(&xsk.umem->fq, fq_index++) = alloc_frame();
        }

        xsk_ring_prod__submit(&xsk.umem->fq, free_cnt);
    }
}

// resend ipv6 udp packets on rx ring to the other process listening on another network card
// drop other kinds of packets
static void rx_resend() {
	u32 rx_index = 0, rx_num = 0;
	int ret;

	// consume the rx ring
	rx_num = xsk_ring_cons__peek(&xsk.rx, cfg.batch_size, &rx_index);
	if(rx_num == 0) {
		return;
	}

	struct timespec ts;
	get_monotonic_time(&ts);
	long timestamp = get_time_nano(&ts);
	printf("receive %lld %ld\n", xsk.ring_stats.rx_npkts, timestamp);
	fill_fill_ring();
	
	xsk.ring_stats.rx_npkts += rx_num;
	// printf("received %d packets\n", rx_num);
	// process the data packet here
	for(u32 i = 0; i<rx_num; i++) {
		const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&xsk.rx, rx_index);
		ret = handle_packet(desc);
		if(ret == 0) {
			free_frame(desc->addr);
		}
		rx_index++;
	}
	
	// make sure the data bas been read before calling this function
	xsk_ring_cons__release(&xsk.rx, rx_num);
}


// process packets on rx ring in a loop
static void rx_loop() {
	// struct pollfd fd;
	// fd.fd = xsk_socket__fd(xsk.xsk);
	// fd.events = POLLIN;

	while(!should_exit) {
		rx_resend();
	}
}


static void load_xdp_program(const char* path, const char* if_name) {
	struct bpf_object* obj;
	struct bpf_map *map;
	int prog_fd, xsks_map;
	int ret;
	struct bpf_prog_load_attr load_attr = {
		.prog_type = BPF_PROG_TYPE_XDP,
		.file = path
	};

	ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
	if(ret != 0) {
		exit_with_error("bpf_prog_load_xattr failed");
	}
	int if_index = if_nametoindex(if_name);
	if(if_index == 0) {
		exit_with_error("if_nametoindex failed");
	}
	ret = bpf_set_link_xdp_fd(if_index, prog_fd, cfg.xdp_flags);
	if(ret < 0) {
		exit_with_error("bpf_set_link_xdp_fd failed");
	}

	map = bpf_object__find_map_by_name(obj, "xsks_map");
	xsks_map = bpf_map__fd(map);
	if(xsks_map < 0) {
		exit_with_error("xsks_map not found");
	}

	int key = cfg.queue_id, xsk_fd;
	xsk_fd = xsk_socket__fd(xsk.xsk);
	ret = bpf_map_update_elem(xsks_map, &key, &xsk_fd, 0);
	if(ret != 0) {
		exit_with_error("bpf_map_update_elem failed");
	}
}


int main(int argc, char* argv[]) {
	if(argc < 2) {
		printf("usage: ./worker {dev_name} [--send]\n");
		return 0;
	}

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    struct sockaddr_un addr;
    client_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(client_fd < 0) {
        exit_with_error("AF_UNIX socket creation failed");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    int ret = connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr));
    if(ret == -1) {
        exit_with_error("cannot connect to manager");
    }

    cfg = get_default_config();
	cfg.device_name = argv[1];
	printf("device name: %s\n", argv[1]);

	if(argc >= 3 && strcmp(argv[2], "--send") == 0) {
		printf("resend packet\n");
		cfg.opt_send = 1;
	}

    struct xsk_umem_info* umem = create_umem();

    create_socket(umem);
	//setup_socket_options();
	load_xdp_program(cfg.xdp_program_path, cfg.device_name);

    rx_loop();

    close(client_fd);

    return 0;
}