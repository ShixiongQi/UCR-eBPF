#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#define barrier() __asm__ __volatile__("": : :"memory")
#define u_smp_rmb() barrier()
#define u_smp_wmb() barrier()

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
#include <sys/mman.h>
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
#include <bpf/libbpf.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <errno.h>
#include <time.h>
#include "extra_definitions.h"


// the queue for fill ring and complete ring
struct xdp_umem_queue {
	u32 cached_prod;
	u32 cached_cons;
	u32 mask;
	u32 size;
	u32 *producer;
	u32 *consumer;
	u64 *ring;
	void *map;
};


struct xdp_umem {
	void* frames;
	// the fill ring
	struct xdp_umem_queue fq;
	// the completion ring
	struct xdp_umem_queue cq;
	int fd;
};


// the queue for rx and tx ring
struct xdp_queue {
	u32 cached_prod;
	u32 cached_cons;
	u32 mask;
	u32 size;
	u32 *producer;
	u32 *consumer;
	struct xdp_desc *ring;
	void *map;
};

struct xdp_sock {
	struct xdp_umem *umem;
	struct xdp_queue rx;
	struct xdp_queue tx;
	int fd;
};

struct xdp_program {
	struct bpf_object* obj;
	int prog_fd;
};

struct pkt_context {
	const struct xdp_desc* desc;
	u8* pkt;
	struct ethhdr* eth;
	struct iphdr* ip;
	struct ipv6hdr* ipv6;
	struct udphdr* udp;
};


static int should_exit = 0;

static void int_exit(int sig) {
	should_exit = 1;
}

// create umem
// sfd is the file descriptor for socket
static struct xdp_umem* create_umem(int sfd) {
	struct xdp_umem *umem = calloc(1, sizeof(*umem));
	if(!umem) {
        exit_with_error("memory allocation failed");
    }

	int ret = posix_memalign(&umem->frames, getpagesize(), DEFAULT_UMEM_SIZE);
	if(ret != 0) {
		exit_with_error("memory allocation with aligned pages failed");
	}

	struct xdp_umem_reg reg;
	reg.addr = (u64)umem->frames;
	reg.len = DEFAULT_UMEM_SIZE;
	reg.chunk_size = DEFAULT_FRAME_SIZE;
	reg.headroom = DEFAULT_HEADROOM;
	reg.flags = 0;

	ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
	if(ret != 0) {
		exit_with_error("setsockopt failed");
	}

	int fq_size = DEFAULT_FILL_RING_SIZE;
	int cq_size = DEFAULT_COMPLETION_RING_SIZE;

	ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_FILL_RING, &fq_size, sizeof(int));
	if(ret != 0) {
		exit_with_error("setsockopt failed");
	}

	ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cq_size, sizeof(int));
	if(ret != 0) {
		exit_with_error("setsockopt failed");
	}

	struct xdp_mmap_offsets off;
	socklen_t optlen = sizeof(off);
	ret = getsockopt(sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	if(ret != 0) {
		exit_with_error("getsockopt failed");
	}
	
	// fill ring
	umem->fq.map = mmap(0, off.fr.desc + DEFAULT_FILL_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED |MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_FILL_RING);
	if(umem->fq.map == MAP_FAILED) {
		exit_with_error("mmap failed");
	}

	umem->fq.mask = DEFAULT_FILL_RING_SIZE - 1;
	umem->fq.size = DEFAULT_FILL_RING_SIZE;
	umem->fq.producer = umem->fq.map + off.fr.producer;
	umem->fq.consumer = umem->fq.map + off.fr.consumer;
	umem->fq.ring = umem->fq.map + off.fr.desc;
	umem->fq.cached_cons = DEFAULT_FILL_RING_SIZE;

	// completion ring
	umem->cq.map = mmap(0, off.cr.desc + DEFAULT_COMPLETION_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_COMPLETION_RING);
	if(umem->cq.map == MAP_FAILED) {
		exit_with_error("mmap failed");
	}

	umem->cq.mask = DEFAULT_COMPLETION_RING_SIZE - 1;
	umem->cq.size = DEFAULT_COMPLETION_RING_SIZE;
	umem->cq.producer = umem->cq.map + off.cr.producer;
	umem->cq.consumer = umem->cq.map + off.cr.consumer;
	umem->cq.ring = umem->cq.map + off.cr.desc;

	umem->fd = sfd;

	return umem;
}

// create a af_xdp socket
static struct xdp_sock* create_socket(int if_index) {
	int fd = socket(AF_XDP, SOCK_RAW, 0);
	if(fd == -1) {
		exit_with_error("socket call failed");
	}

	struct xdp_sock *sock = calloc(1, sizeof(*sock));
	if(!sock) {
		exit_with_error("memory allocation failed");
	}

	sock->fd = fd;
	sock->umem = create_umem(fd);

	int rx_ring_size = DEFAULT_RX_RING_SIZE;
	int tx_ring_size = DEFAULT_TX_RING_SIZE;
	int ret = setsockopt(fd, SOL_XDP, XDP_RX_RING, &rx_ring_size, sizeof(int));
	if(ret != 0) {
		exit_with_error("setsockopt failed");
	}

	ret = setsockopt(fd, SOL_XDP, XDP_TX_RING, &tx_ring_size, sizeof(int));
	if(ret != 0) {
		exit_with_error("setsockopt failed");
	}

	struct xdp_mmap_offsets off;
	socklen_t optlen = sizeof(off);
	ret = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	if(ret != 0) {
		exit_with_error("getsockopt failed");
	}

	// rx ring
	sock->rx.map = mmap(0, off.rx.desc + DEFAULT_RX_RING_SIZE * sizeof(struct xdp_desc),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE, fd,
						XDP_PGOFF_RX_RING);
	if(sock->rx.map == MAP_FAILED) {
		exit_with_error("mmap failed");
	}

	sock->rx.mask = DEFAULT_RX_RING_SIZE - 1;
	sock->rx.size = DEFAULT_RX_RING_SIZE;
	sock->rx.producer = sock->rx.map + off.rx.producer;
	sock->rx.consumer = sock->rx.map + off.rx.consumer;
	sock->rx.ring = sock->rx.map + off.rx.desc;

	// tx ring
	sock->tx.map = mmap(0, off.tx.desc + DEFAULT_TX_RING_SIZE * sizeof(struct xdp_desc),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE, fd,
						XDP_PGOFF_TX_RING);
	if(sock->tx.map == MAP_FAILED) {
		exit_with_error("mmap failed");
	}

	sock->tx.mask = DEFAULT_TX_RING_SIZE - 1;
	sock->tx.size = DEFAULT_TX_RING_SIZE;
	sock->tx.producer = sock->tx.map + off.tx.producer;
	sock->tx.consumer = sock->tx.map + off.tx.consumer;
	sock->tx.cached_cons = DEFAULT_TX_RING_SIZE;

	// bind addr
	struct sockaddr_xdp addr;
	addr.sxdp_family = AF_XDP;
	addr.sxdp_flags = 0;
	addr.sxdp_ifindex = if_index;
	addr.sxdp_queue_id = 0;
	addr.sxdp_shared_umem_fd = 0;

	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if(ret != 0) {
		exit_with_error("bind failed");
	}

	return sock;
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


static struct xdp_program* load_xdp_program(const char* path, int if_index) {
	struct xdp_program *program = calloc(1, sizeof(struct xdp_program));
	if(!program) {
		exit_with_error("memory allocation failed");
	}

	int ret;
	struct bpf_prog_load_attr load_attr = {
		.prog_type = BPF_PROG_TYPE_XDP,
		.file = path
	};

	ret = bpf_prog_load_xattr(&load_attr, &program->obj, &program->prog_fd);
	if(ret != 0) {
		exit_with_error("bpf_prog_load_xattr failed");
	}

	ret = bpf_set_link_xdp_fd(if_index, program->prog_fd, XDP_FLAGS_DRV_MODE);
	if(ret < 0) {
		exit_with_error("bpf_set_link_xdp_fd failed");
	}

	return program;
}

static void update_xdp_map(struct xdp_program* program, const char* map_name, int key, int value) {
	struct bpf_map *map = bpf_object__find_map_by_name(program->obj, map_name);
	int map_fd = bpf_map__fd(map);
	if(map_fd < 0) {
		exit_with_error("xsks_map not found");
	}

	int ret = bpf_map_update_elem(map_fd, &key, &value, 0);
	if(ret != 0) {
		exit_with_error("bpf_map_update_elem failed");
	}
}

// return the number of free entries in umem fill or completion ring
static inline u32 umem_nb_free(struct xdp_umem_queue *q, u32 nb) {
	u32 free_entries = q->cached_cons - q->cached_prod;

	if(free_entries >= nb) {
		return free_entries;
	}

	q->cached_cons = *q->consumer + q->size;
	return q->cached_cons - q->cached_prod;
}

// return the number of free entries in rx or tx ring
static inline u32 xq_nb_free(struct xdp_queue *q, u32 nb) {
	u32 free_entries = q->cached_cons - q->cached_prod;

	if(free_entries >= nb) {
		return free_entries;
	}

	q->cached_cons = *q->consumer + q->size;
	return q->cached_cons - q->cached_prod;
}

// return the number of usable entries in umem fill or completion ring
static inline u32 umem_nb_avail(struct xdp_umem_queue *q, u32 nb) {
	u32 entries = q->cached_prod - q->cached_cons;

	if(entries == 0) {
		q->cached_prod = *q->producer;
		entries = q->cached_prod - q->cached_cons;
	}

	return (entries > nb) ? nb : entries;
}

// return the number of usable entries in rx or tx ring
static inline u32 xq_nb_avail(struct xdp_queue *q, u32 nb) {
	u32 entries = q->cached_prod - q->cached_cons;

	if(entries == 0) {
		q->cached_prod = *q->producer;
		entries = q->cached_prod - q->cached_cons;
	}

	return (entries > nb) ? nb : entries;
}

// fill descriptors into the fill ring
static inline size_t umem_fill_to_kernel(struct xdp_umem_queue *fq, u64 *d, size_t nb) {
	if(umem_nb_free(fq, nb) < nb) {
		return -1;
	}

	for(u32 i = 0; i<nb; i++) {
		u32 idx = fq->cached_prod++ &fq->mask;
		fq->ring[idx] = d[i];
	}

	u_smp_wmb();

	*fq->producer = fq->cached_prod;

	return nb;
}

// get descriptors from the completion ring
static inline size_t umem_complete_from_kernel(struct xdp_umem_queue *cq, u64 *d, size_t nb) {
	u32 entries = umem_nb_avail(cq, nb);

	u_smp_rmb();

	for(u32 i = 0; i < entries; i++) {
		u32 idx = cq->cached_cons++ & cq->mask;
		d[i] = cq->ring[idx];
	}

	if(entries > 0) {
		u_smp_wmb();
		*cq->consumer = cq->cached_cons;
	}

	return entries;
}

// get packet data from descriptor in rx ring
static inline void *xq_get_data(const struct xdp_sock *sock, u64 desc) {
	return &sock->umem->frames[desc];
}

// get descriptors from rx ring
static inline u32 xq_deq(struct xdp_queue* q, struct xdp_desc* descs, int ndescs) {
	struct xdp_desc *r = q->ring;
	u32 entries = xq_nb_avail(q, ndescs);

	u_smp_rmb();

	for(u32 i=0; i<entries; i++) {
		u32 idx = q->cached_cons++ & q->mask;
		descs[i] = r[idx];
	}

	if(entries > 0) {
		u_smp_wmb();

		*q->consumer = q->cached_cons;
	}

	return entries;
}

static int handle_packet(const struct xdp_sock* sock, const struct xdp_desc* desc) {
	struct pkt_context ctx;
	ctx.desc = desc;
	ctx.pkt = xq_get_data(sock, desc->addr);
	
	ctx.eth = (struct ethhdr*)ctx.pkt;

	u16 proto = ntohs(ctx.eth->h_proto);
	switch (proto)
	{
		case ETH_P_IP:
			ctx.ip = (struct iphdr*)(ctx.eth + 1);
			printf("IP packet\n");
			return 0;
		case ETH_P_IPV6:
			ctx.ipv6 = (struct ipv6hdr*)(ctx.eth + 1);
			printf("IPV6 packet\n");
			return 0;
		default:
			printf("packet type: %d\n", proto);
			break;
	}
	return 0;
}

static void rx_drop(struct xdp_sock* sock) {
	struct xdp_desc desc;
	u32 entries = xq_deq(&sock->rx, &desc, 1);
	if(entries == 0) {
		return;
	}

	handle_packet(sock, &desc);

	// free_frame(desc.addr);
}

static void rx_loop(struct xdp_sock* sock) {
	while(!should_exit) {
		rx_drop(sock);
	}
}

int main(int argc, char* argv[]) {
	allow_unlimited_locking();

	if(argc != 2) {
		printf("usage: xsk {interface_name}\n");
		return 0;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	int if_index = if_nametoindex(argv[1]);
	if(if_index == 0) {
		exit_with_error("if_nametoindex failed");
	}

	struct xdp_program* program = load_xdp_program("./kern.o", if_index);

	struct xdp_sock* sock = create_socket(if_index);
	u64 descs[DEFAULT_FILL_RING_SIZE];
	for(u32 i=0; i<DEFAULT_FILL_RING_SIZE; i++) {
		descs[i] = i * DEFAULT_FRAME_SIZE;
	}
	umem_fill_to_kernel(&sock->umem->fq, descs, DEFAULT_FILL_RING_SIZE);

	update_xdp_map(program, "xsks_map", 0, sock->fd);

	rx_loop(sock);

	return 0;
}