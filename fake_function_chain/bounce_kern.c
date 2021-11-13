#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";