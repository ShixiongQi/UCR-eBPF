#ifndef __sum16
typedef unsigned short __sum16;
#endif

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


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
	if(index == 0 && bpf_map_lookup_elem(&xsks_map, &index)) {
        return bpf_redirect_map(&xsks_map, 0, 0);
    }

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";