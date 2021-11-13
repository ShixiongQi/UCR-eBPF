#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200112L

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

static void int_exit(int sig) {
	exit(-1);
}


int main(int argc, char* argv[]) {
    if(argc != 2) {
		printf("usage: bounce {interface_name}\n");
		return 0;
	}
    int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);

    int ret;
    int prog_fd;

    struct bpf_object* obj;

    struct bpf_prog_load_attr load_attr = {
        .file = "./bounce_kern.o",
        .prog_type = BPF_PROG_TYPE_XDP
    };

    ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
    assert(ret == 0);

    ret = bpf_set_link_xdp_fd(if_index, prog_fd, XDP_FLAGS_DRV_MODE);
    assert(ret >= 0);
    
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
    while(true) {
        sleep(1);
    }
}