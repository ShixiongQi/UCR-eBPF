#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{
	bpf_printk("[bouncer] get a packet");
	void* data = (void*)(long)ctx->data;
	if((char*)(data+44) > (char*)(long)ctx->data_end)
	{
		return XDP_TX;
	}

	data = ((char*)data) + 16;
	int magic = *((int*)data);
	if(magic != 0x12345678)
	{
		return XDP_TX;
	}

	int function_id = *(((int*)data) + 1);
	bpf_printk("[bouncer] function id is %d", function_id);

	return XDP_TX;
}

char _license[] SEC("license") = "GPL";