#define _GNU_SOURCE 

#include <stdlib.h>
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
#include <sys/socket.h>
#include <linux/socket.h>
#include <bpf/libbpf.h>
#include <bpf/xsk.h>
#include <bpf/bpf.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include "extra_definitions.h"

// the number of frames, default frame size is XSK_UMEM__DEFAULT_FRAME_SIZE, which is 4k
#define DEFAULT_NUM_FRAMES    (4*1024)
#define DEFAULT_FRAME_SIZE    XSK_UMEM__DEFAULT_FRAME_SIZE
#define DEFAULT_PROD_NUM_DESCS    XSK_RING_PROD__DEFAULT_NUM_DESCS
#define DEFAULT_CONS_NUM_DESCS    XSK_RING_CONS__DEFAULT_NUM_DESCS
#define DEFAULT_HEADROOM   0
#define DEFAULT_FLAGS    0

typedef __u64 u64;
typedef __u32 u32;
typedef __u16 u16;
typedef __u8  u8;


struct xsk_ring_stats {
	u64 rx_npkts;
	u64 tx_npkts;
	u64 rx_dropped_npkts;
	u64 rx_invalid_npkts;
	u64 tx_invalid_npkts;
	u64 rx_full_npkts;
	u64 rx_fill_empty_npkts;
	u64 tx_empty_npkts;
	u64 prev_rx_npkts;
	u64 prev_tx_npkts;
	u64 prev_rx_dropped_npkts;
	u64 prev_rx_invalid_npkts;
	u64 prev_tx_invalid_npkts;
	u64 prev_rx_full_npkts;
	u64 prev_rx_fill_empty_npkts;
	u64 prev_tx_empty_npkts;
};


struct xsk_app_stats {
	u64 rx_empty_polls;
	u64 fill_fail_polls;
	u64 copy_tx_sendtos;
	u64 tx_wakeup_sendtos;
	u64 opt_polls;
	u64 prev_rx_empty_polls;
	u64 prev_fill_fail_polls;
	u64 prev_copy_tx_sendtos;
	u64 prev_tx_wakeup_sendtos;
	u64 prev_opt_polls;
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
	u64 free_frames_array[DEFAULT_NUM_FRAMES];
	u32 free_frame_cnt;
};


struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
    // The UMEM uses two rings: FILL and COMPLETION.
    // Each socket associated with the UMEM must have an RX queue, TX queue or both.
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	struct xsk_ring_stats ring_stats;
	struct xsk_app_stats app_stats;
	struct xsk_driver_stats drv_stats;
	u32 outstanding_tx;
};


struct config {
	int queue_id;
	u32 batch_size;
	u32 xdp_flags;
	const char* device_name;
	const char* xdp_program_path;
};

static struct config cfg;
static bool should_exit = false;


static void __exit_with_error(const char* error, const char* file, const char* func, int line) {
    fprintf(stderr, "error: %s \n  at %s %d %s\n", error, file, line, func);
    exit(EXIT_FAILURE);
}

#define exit_with_error(error)  __exit_with_error(error, __FILE__, __func__, __LINE__)


static struct config get_default_config() {
	struct config cfg = {
		.queue_id = 0,
		.batch_size = 64,
		.xdp_flags = XDP_FLAGS_DRV_MODE,
		.device_name = "test2",
		.xdp_program_path = "./kern.o"
	};

	return cfg;
}

// allocate a umem frame, return the descriptor
static u64 alloc_frame(struct xsk_umem_info* umem) {
	if(umem->free_frame_cnt == 0) {
		exit_with_error("umem frame allocation failed");
	}

	u64 frame = umem->free_frames_array[--umem->free_frame_cnt];
	//printf("allocate %p\n", (void*)frame);
	return frame;
}

// free a umem frame.
static void free_frame(struct xsk_umem_info* umem, u64 frame) {
	// based on my experiment, it seems that rx ring does not always return aligned address
	frame = frame & (~(DEFAULT_FRAME_SIZE-1));
	if(umem->free_frame_cnt == DEFAULT_NUM_FRAMES) {
		exit_with_error("free more times than allocation times");
	}
	umem->free_frames_array[umem->free_frame_cnt++] = frame;
	//printf("free %p\n", (void*)frame);
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


static void int_exit(int sig) {
	should_exit = true;
}

// create and initialize a xsk_umem_info struct
static struct xsk_umem_info* create_umem() {
	int result;
	u32 idx;

    struct xsk_umem_info *umem = calloc(1, sizeof(*umem));
    if(!umem) {
        exit_with_error("memory allocation failed");
    }

    size_t umem_len = DEFAULT_NUM_FRAMES * DEFAULT_FRAME_SIZE;
    // reserve memory for umem
    // it is a good strategy to use malloc for tiny objects and mmap for large ones
    umem->buffer = mmap(NULL, umem_len,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (umem->buffer == MAP_FAILED) {
        exit_with_error("mmap failed");
    }

	umem->free_frame_cnt = DEFAULT_NUM_FRAMES;
	for(int i=0; i<DEFAULT_NUM_FRAMES; i++) {
		umem->free_frames_array[i] = i * DEFAULT_FRAME_SIZE;
	}

    struct xsk_umem_config cfg = {
        .fill_size = DEFAULT_NUM_FRAMES,
        .comp_size = DEFAULT_NUM_FRAMES,
        .frame_size = DEFAULT_FRAME_SIZE,
        .frame_headroom = DEFAULT_HEADROOM,
        .flags = DEFAULT_FLAGS
    };

    result = xsk_umem__create(&umem->umem, umem->buffer, umem_len, &umem->fq, &umem->cq, &cfg);
    if(result) {
        exit_with_error("xsk_umem__create failed");
    }

	////////
	// populate the fill ring of an umem
	// idx is the cached_prod before reserving
	result = xsk_ring_prod__reserve(&umem->fq, DEFAULT_NUM_FRAMES, &idx);
	if(result != DEFAULT_NUM_FRAMES) {
		exit_with_error("xsk_ring_prod__reserve failed");
	}
	for(int i=0; i<DEFAULT_NUM_FRAMES; i++) {
		// set the addr in ring to be the offset from the beginning of umem buffer
		*xsk_ring_prod__fill_addr(&umem->fq, idx++) = alloc_frame(umem);
	}

	xsk_ring_prod__submit(&umem->fq, DEFAULT_PROD_NUM_DESCS);

    return umem;
}


// create a af_xdp socket
static struct xsk_socket_info* create_socket(struct xsk_umem_info* umem) {
	struct xsk_socket_config socket_cfg;
	struct xsk_socket_info *xsk;

	xsk = calloc(1, sizeof(*xsk));
	if(!xsk) {
		exit_with_error("memory allocation failed");
	}

	xsk->umem = umem;
	socket_cfg.rx_size = DEFAULT_CONS_NUM_DESCS;
	socket_cfg.tx_size = DEFAULT_PROD_NUM_DESCS;
	socket_cfg.libbpf_flags = 0;
	socket_cfg.xdp_flags = XDP_FLAGS_DRV_MODE;
	// The kernel will first try to use zero-copy copy.
	// If zero-copy is not supported, it will fall back on using copy mode
	// Of course, we can force a certain mode if we want to.
	socket_cfg.bind_flags = 0; //XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;

	int ret = xsk_socket__create(&xsk->xsk, cfg.device_name, cfg.queue_id, xsk->umem->umem, &xsk->rx, &xsk->tx, &socket_cfg);
	if(ret) {
		exit_with_error("xsk_socket__create failed");
	}

	return xsk;
}

// set up busy poll options for socket
static void setup_socket_options(struct xsk_socket_info* xsk) {
	int ret;
	int sock_opt;
	int sock_fd = xsk_socket__fd(xsk->xsk);
	
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


static void handle_ip_packet(struct ethhdr* eth) {
	printf("IP\n");
	struct iphdr* ip = (struct iphdr*)(eth + 1);
}

static void handle_ipv6_packet(struct ethhdr* eth) {
	printf("IPV6\n");
	struct ipv6hdr* ipv6 = (struct ipv6hdr*)(eth + 1);
}

// the logic to process the packet
static void handle_packet(struct xsk_socket_info* xsk, const struct xdp_desc* desc) {
	u8* pkt = xsk_umem__get_data(xsk->umem->buffer, desc->addr);
	printf("addr: %p length: %d\n", pkt, desc->len);

	struct ethhdr *eth = (struct ethhdr*)pkt;
	u16 proto = ntohs(eth->h_proto);
	switch (proto)
	{
		case ETH_P_IP:
			handle_ip_packet(eth);
			break;
		case ETH_P_IPV6:
			handle_ipv6_packet(eth);
			break;
		default:
			printf("packet type: %d\n", proto);
			break;
	}
}

// drop packets on rx ring
static void rx_drop(struct xsk_socket_info* xsk) {
	u32 rx_index = 0, rx_num = 0;
	int ret;

	// consume the rx ring
	rx_num = xsk_ring_cons__peek(&xsk->rx, cfg.batch_size, &rx_index);
	if(rx_num == 0) {
		return;
	}
	// produce the fill ring as much as possible
	if(xsk->umem->free_frame_cnt > 0) {
		u32 fq_index;
		u32 free_cnt = xsk_prod_nb_free(&xsk->umem->fq, xsk->umem->free_frame_cnt);
		// the actual number of frames we can produce
		u32 usable_cnt = MIN(free_cnt, xsk->umem->free_frame_cnt);
		// This should not happen, but check it just in case
		if(xsk_ring_prod__reserve(&xsk->umem->fq, usable_cnt, &fq_index) != usable_cnt) {
			exit_with_error("cannot reserve frames");
		}
		
		for(u32 i = 0; i<usable_cnt; i++) {
			*xsk_ring_prod__fill_addr(&xsk->umem->fq, fq_index++) = alloc_frame(xsk->umem);
		}

		xsk_ring_prod__submit(&xsk->umem->fq, usable_cnt);
	}
	

	xsk->ring_stats.rx_npkts += rx_num;
	printf("received %d packets\n", rx_num);
	// process the data packet here
	for(u32 i = 0; i<rx_num; i++) {
		const struct xdp_desc* desc = xsk_ring_cons__rx_desc(&xsk->rx, rx_index);
		handle_packet(xsk, desc);
		free_frame(xsk->umem, desc->addr);
		rx_index++;
	}
	
	// make sure the data bas been read before calling this function
	xsk_ring_cons__release(&xsk->rx, rx_num);
}


// drop all incoming packets
static void rx_drop_all(struct xsk_socket_info* xsk) {
	struct pollfd fd;
	fd.fd = xsk_socket__fd(xsk->xsk);
	fd.events = POLLIN;

	while(!should_exit) {
		rx_drop(xsk);
	}
}

static void load_xdp_program(const char* path, const char* if_name, struct xsk_socket_info* xsk) {
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

	int key = 0, xsk_fd;
	xsk_fd = xsk_socket__fd(xsk->xsk);
	ret = bpf_map_update_elem(xsks_map, &key, &xsk_fd, 0);
	if(ret != 0) {
		exit_with_error("bpf_map_update_elem failed");
	}
}


int main() {
	allow_unlimited_locking();
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	cfg = get_default_config();
	
    struct xsk_umem_info* umem = create_umem();
	
	struct xsk_socket_info* xsk = create_socket(umem);
	setup_socket_options(xsk);
	//load_xdp_program(cfg.xdp_program_path, cfg.device_name, xsk);

	rx_drop_all(xsk);

	return 0;
} 