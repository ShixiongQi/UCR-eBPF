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
#include <sys/syscall.h>
#include <bpf/bpf.h>
#include <bpf/xsk.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <bits/stdc++.h>

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL	69
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#ifndef __NR_pidfd_getfd
#define __NR_pidfd_getfd 438
#endif


typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

namespace DEFAULT {
	// the size of a single frame
	constexpr int FRAME_SIZE = 4096;
	// the number of frames
	constexpr int NUM_FRAMES = 16*1024;
	// the size of fill ring
	constexpr int FILL_RING_SIZE = 1024;
	// the size of completion ring
	constexpr int COMPLETION_RING_SIZE = 1024;
	// the size of rx ring
	constexpr int RX_RING_SIZE = 1024;
	// the size of tx ring
	constexpr int TX_RING_SIZE = 1024;
	// the total size of umem
	constexpr int UMEM_SIZE = NUM_FRAMES * FRAME_SIZE;
	constexpr const char* UMEM_FILE_NAME = "share_umem";
	constexpr int HEADROOM_SIZE = 0;
	constexpr int UMEM_FLAGS = 0;

	constexpr const char* SOCKET_PATH = "/tmp/share_umem_manager.socket";
	constexpr int SOCKET_MAX_BUFFER_SIZE = 32;

	constexpr int BPF_MAP_MAX_ENTRIES = 1024;
}


enum class RequestType : u32 {
	// In: none Out: u64
	ALLOC,
	// In: u64 Out: none
	DEALLOC,
	// In: none Out: int(pid_t) and int
	GET_PID_FD,
	// In: none Out: pid(int) program_fd(int) map_fd(int)
	GET_BPF_PROGRAM,
	MAX
};


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

	// return the number of free entries in umem fill ring
	inline u32 fr_nb_free() {
		cached_cons = *consumer + size;
		return cached_cons - cached_prod;
	}

	// return the number of usable entries in umem completion ring
	inline u32 cr_nb_avail() {
		cached_prod = *producer;
		return cached_prod - cached_cons;
	}

	// enqueue descriptors into the fill ring
	inline u32 fr_enq(u64 *descs, u32 nb) {
		if(fr_nb_free() < nb) {
			return -1;
		}

		for(u32 i = 0; i<nb; i++) {
			u32 idx = cached_prod++ &mask;
			ring[idx] = descs[i];
		}

		u_smp_wmb();

		*producer = cached_prod;

		return nb;
	}

	// dequeue descriptors from the completion ring
	inline u32 cr_deq(u64 *descs, u32 nb) {
		if(cr_nb_avail() < nb) {
			return -1;
		}

		u_smp_rmb();

		for(u32 i = 0; i < nb; i++) {
			u32 idx = cached_cons++ & mask;
			descs[i] = ring[idx];
		}

		u_smp_wmb();
		*consumer = cached_cons;

		return nb;
	}
};


// struct containing information for shared umem
struct xdp_umem_shared {
	u8* frames;
	int fd;
	// get packet data from addr in descriptor of rx ring
	inline u8* get_data(u64 addr) {
		return &frames[addr];
	}
	void map_shared_memory() {
		int mem_fd = shm_open(DEFAULT::UMEM_FILE_NAME, O_RDWR, 0);
		assert(mem_fd >= 0);

		frames = (u8*)mmap(nullptr, DEFAULT::UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
		assert(frames != MAP_FAILED);
	}

	void get_shared_fd(int client_fd) {
		u8 buffer[DEFAULT::SOCKET_MAX_BUFFER_SIZE];
		*((u32*)buffer) = (u32)RequestType::GET_PID_FD;
		int ret = send(client_fd, buffer, sizeof(u32), MSG_EOR);
		assert(ret != -1);
		ret = recv(client_fd, buffer, sizeof(buffer), 0);
		assert(ret != -1);
		assert(ret != 0);

		pid_t pid = *((pid_t*)buffer);
		int remote_fd = *((int*)(buffer + sizeof(pid_t)));
		// printf("pid: %d remote_fd: %d\n", pid, remote_fd);
		int pidfd = syscall(SYS_pidfd_open, pid, 0);
		assert(pidfd != -1);

		fd = syscall(__NR_pidfd_getfd, pidfd, remote_fd, 0);
		assert(fd != -1);
	}
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

	// return the number of free entries in tx ring
	inline u32 tx_nb_free() {
		cached_cons = *consumer + size;
		return cached_cons - cached_prod;
	}


	// return the number of usable entries in tx ring
	inline u32 rx_nb_avail() {
		cached_prod = *producer;
		return cached_prod - cached_cons;
	}


	// get descriptors from rx ring
	inline u32 rx_deq(xdp_desc* descs, u32 nb) {
		if(rx_nb_avail() < nb) {
			return -1;
		}

		u_smp_rmb();

		for(u32 i=0; i<nb; i++) {
			u32 idx = cached_cons++ & mask;
			descs[i] = ring[idx];
		}

		u_smp_wmb();
		*consumer = cached_cons;

		return nb;
	}

	// send data to tx ring
	inline u32 tx_enq(xdp_desc* descs, u32 nb) {
		if(tx_nb_free() < nb) {
			return -1;
		}

		for(u32 i = 0; i<nb; i++) {
			u32 idx = cached_prod++ & mask;
			ring[idx] = descs[idx];
		}

		u_smp_wmb();

		*producer = cached_prod;
		return nb;
	}
};

// struct containing information for a socket
struct xdp_sock {
	// the rx ring
	struct xdp_queue rxr;
	// the tx ring
	struct xdp_queue txr;
	int fd;

	void create() {
		int rx_ring_size = DEFAULT::RX_RING_SIZE;
		int tx_ring_size = DEFAULT::TX_RING_SIZE;
		int ret = setsockopt(fd, SOL_XDP, XDP_RX_RING, &rx_ring_size, sizeof(int));
		assert(ret == 0);
		ret = setsockopt(fd, SOL_XDP, XDP_TX_RING, &tx_ring_size, sizeof(int));
		assert(ret == 0);

		struct xdp_mmap_offsets off;
		socklen_t optlen = sizeof(off);
		ret = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
		assert(ret == 0);
		
		// rx ring
		rxr.map = (u8*)(mmap(0, off.rx.desc + DEFAULT::RX_RING_SIZE * sizeof(xdp_desc),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE, fd,
						XDP_PGOFF_RX_RING));
		assert(rxr.map != MAP_FAILED);

		rxr.mask = DEFAULT::RX_RING_SIZE - 1;
		rxr.size = DEFAULT::RX_RING_SIZE;
		rxr.producer = (u32*)(rxr.map + off.rx.producer);
		rxr.consumer = (u32*)(rxr.map + off.rx.consumer);
		rxr.ring = (xdp_desc*)(rxr.map + off.rx.desc);


		// tx ring
		txr.map = (u8*)(mmap(0, off.tx.desc + DEFAULT::TX_RING_SIZE * sizeof(xdp_desc),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE, fd,
						XDP_PGOFF_TX_RING));
		assert(txr.map != MAP_FAILED);

		txr.mask = DEFAULT::TX_RING_SIZE - 1;
		txr.size = DEFAULT::TX_RING_SIZE;
		txr.producer = (u32*)(txr.map + off.tx.producer);
		txr.consumer = (u32*)(txr.map + off.tx.consumer);
		txr.ring = (xdp_desc*)(txr.map + off.tx.desc);
		txr.cached_cons = DEFAULT::TX_RING_SIZE;
	}

	void bind_to_device_shared(int if_index, int shared_fd) {
		// bind shared umem
		sockaddr_xdp addr;
		addr.sxdp_family = AF_XDP;
		addr.sxdp_flags = XDP_SHARED_UMEM;
		addr.sxdp_ifindex = if_index;
		addr.sxdp_queue_id = 0;
		addr.sxdp_shared_umem_fd = shared_fd;

		int ret = bind(fd, (const sockaddr*)(&addr), sizeof(addr));
		printf("fd: %d shared_fd: %d\n", fd, shared_fd);
		printf("errno: %d description: %s\n", errno, strerror(errno));
		assert(ret == 0);
	}

	void bind_to_device(int if_index) {
		sockaddr_xdp addr;
		addr.sxdp_family = AF_XDP;
		addr.sxdp_flags = 0;
		addr.sxdp_ifindex = if_index;
		addr.sxdp_queue_id = 0;
		addr.sxdp_shared_umem_fd = 0;
		
		int ret = bind(fd, (const sockaddr*)(&addr), sizeof(addr));
		assert(ret == 0);
	}
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

// struct containing information for ebpf kernel program
struct xdp_program {
	int prog_fd;
	int map_fd;

	void load(const char* path, int if_index) {
		int ret;

		struct bpf_object* obj;

		struct bpf_prog_load_attr load_attr = {
			.file = path,
			.prog_type = BPF_PROG_TYPE_XDP
		};

		ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
		assert(ret == 0);

		ret = bpf_set_link_xdp_fd(if_index, prog_fd, XDP_FLAGS_DRV_MODE);
		assert(ret >= 0);
		
		map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
		assert(map_fd >= 0);
	}

	void get_shared(int client_fd) {
		u8 buffer[DEFAULT::SOCKET_MAX_BUFFER_SIZE];
		*((u32*)buffer) = (u32)RequestType::GET_BPF_PROGRAM;
		int ret = send(client_fd, buffer, 4, MSG_EOR);
		assert(ret != -1);
		ret = recv(client_fd, buffer, sizeof(buffer), 0);
		assert(ret != -1);
		assert(ret != 0);

		pid_t pid = *((pid_t*)buffer);
		int remote_prog_fd = *((int*)(buffer + 4));
		int remote_map_fd = *((int*)(buffer + 8));
		
		int pidfd = syscall(SYS_pidfd_open, pid, 0);
		assert(pidfd != -1);

		prog_fd = syscall(__NR_pidfd_getfd, pidfd, remote_prog_fd, 0);
		assert(prog_fd != -1);

		map_fd = syscall(__NR_pidfd_getfd, pidfd, remote_map_fd, 0);
		assert(map_fd != -1);
	}

	void update_map(int key, int value) {
		int ret = bpf_map_update_elem(map_fd, &key, &value, 0);
		assert(ret == 0);
	}
};

static void allow_unlimited_locking() {
	struct rlimit lim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};

	int ret = setrlimit(RLIMIT_MEMLOCK, &lim);
	assert(ret == 0);
}


static void int_exit(int sig) {
	exit(-1);
}