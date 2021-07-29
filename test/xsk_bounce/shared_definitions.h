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

