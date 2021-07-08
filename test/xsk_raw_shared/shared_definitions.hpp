#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200112L
#define barrier() __asm__ __volatile__("": : :"memory")
#define u_smp_rmb() barrier()
#define u_smp_wmb() barrier()



#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
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
#include <linux/socket.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <bpf/bpf.h>
#include <bpf/xsk.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL	69
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif


typedef __u64 u64;
typedef __u32 u32;
typedef __u16 u16;
typedef __u8  u8;

// the size of a single frame
constexpr int DEFAULT_FRAME_SIZE = 4096;
// the number of frames
constexpr int DEFAULT_NUM_FRAMES = 16*1024;
// the size of fill ring
constexpr int DEFAULT_FILL_RING_SIZE = 1024;
// the size of completion ring
constexpr int DEFAULT_COMPLETION_RING_SIZE = 1024;
// the size of rx ring
constexpr int DEFAULT_RX_RING_SIZE = 1024;
// the size of tx ring
constexpr int DEFAULT_TX_RING_SIZE = 1024;
// the total size of umem
constexpr int DEFAULT_UMEM_SIZE = DEFAULT_NUM_FRAMES * DEFAULT_FRAME_SIZE;
constexpr const char* DEFAULT_UMEM_FILE_NAME = "share_umem";
constexpr int DEFAULT_HEADROOM = 0;
constexpr int DEFAULT_UMEM_FLAGS = 0;

constexpr const char* DEFAULT_SOCKET_PATH = "/tmp/share_umem_manager.socket";
constexpr int DEFAULT_SOCKET_MAX_BUFFER_SIZE = 32;

constexpr const int REQUEST_ALLOC = 1;
constexpr const int REQUEST_DEALLOC = 2;


// the queue for fill ring and complete ring
struct xdp_umem_queue {
	u32 cached_prod;
	u32 cached_cons;
	u32 mask;
	u32 size;
	u32 *producer;
	u32 *consumer;
	u64 *ring;
	u8 *map;
};


// struct containing information for umem
struct xdp_umem {
	u8* frames;
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
	u8 *map;
};

// struct containing information for a socket
struct xdp_sock {
	struct xdp_umem *umem;
	struct xdp_queue rx;
	struct xdp_queue tx;
	int fd;
};

// struct containing information for ebpf kernel program
struct xdp_program {
	struct bpf_object* obj;
	int prog_fd;
};

// the context of a packet, used in packet parsing
struct pkt_context {
	const struct xdp_desc* desc;
	u8* pkt;
	struct ethhdr* eth;
	struct iphdr* ip;
	struct ipv6hdr* ipv6;
	struct udphdr* udp;
};





// return the number of free entries in rx or tx ring
static inline u32 xq_nb_free(struct xdp_queue *q, u32 nb) {
	u32 free_entries = q->cached_cons - q->cached_prod;

	if(free_entries >= nb) {
		return free_entries;
	}

	q->cached_cons = *q->consumer + q->size;
	return q->cached_cons - q->cached_prod;
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