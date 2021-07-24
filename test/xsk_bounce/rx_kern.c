
#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

struct bpf_map_def SEC("maps") xsks_map = {
	.type = BPF_MAP_TYPE_XSKMAP,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.max_entries = 1024
};


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{
	void* data = (void*)(long)ctx->data;
	if((char*)(data+12) > (char*)(long)ctx->data_end)
	{
		return XDP_DROP;
	}

	int index = *((int*)data);
	if(bpf_map_lookup_elem(&xsks_map, &index))
	{
        return bpf_redirect_map(&xsks_map, index, 0);
    }

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";