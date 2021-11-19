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
	.max_entries = 64
};


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{
	bpf_printk("get a packet, len %d", (ctx->data_end - ctx->data));
	void* data = (void*)(long)ctx->data;
	if((char*)(data+44) > (char*)(long)ctx->data_end)
	{
		bpf_printk("pass a packet, len %d", (ctx->data_end - ctx->data));
		return bpf_redirect_map(&xsks_map, 0, 0);
	}

	data = ((char*)data) + 16;
	int magic = *((int*)data);
	if(magic != 0x12345678)
	{
		return bpf_redirect_map(&xsks_map, 0, 0);
	}

	int function_id = *(((int*)data) + 1);
	bpf_printk("function id is %d", function_id);

	if(bpf_map_lookup_elem(&xsks_map, &function_id))
	{
		return bpf_redirect_map(&xsks_map, function_id, 0);
	}

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";