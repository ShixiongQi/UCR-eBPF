#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* This XDP program is only needed for the XDP_SHARED_UMEM mode.
 * If you do not use this mode, libbpf can supply an XDP program for you.
 */
#define NUM_SOCKS 2

struct bpf_map_def SEC("maps") xsks_map = {
	.type = BPF_MAP_TYPE_XSKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 64
};


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{	
    int index = ctx->rx_queue_index;
	if(bpf_map_lookup_elem(&xsks_map, &index)) {
        return bpf_redirect_map(&xsks_map, index, 0);
    }

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";