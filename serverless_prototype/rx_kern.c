//
// Created by Ziteng Zeng.
//

#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>


/* user accessible metadata for XDP packet hook
 * new fields must be added to the end of this structure
 */
//struct xdp_md {
//    __u32 data;
//    __u32 data_end;
//    __u32 data_meta;
//    /* Below access go through struct xdp_rxq_info */
//    __u32 ingress_ifindex; /* rxq->dev->ifindex */
//    __u32 rx_queue_index;  /* rxq->queue_index  */
//
//    __u32 egress_ifindex;  /* txq->dev->ifindex */
//};

// #define ETH_P_IP	0x0800
// #define ETH_ALEN	6

//struct ethhdr {
//    unsigned char	h_dest[ETH_ALEN];	/* destination eth addr	*/
//    unsigned char	h_source[ETH_ALEN];	/* source ether addr	*/
//    __be16		h_proto;		/* packet type ID field	*/
//} __attribute__((packed));


//struct iphdr {
//#if defined(__LITTLE_ENDIAN_BITFIELD)
//    __u8	ihl:4,
//		version:4;
//#elif defined (__BIG_ENDIAN_BITFIELD)
//    __u8	version:4,
//  		ihl:4;
//#else
//#error	"Please fix <asm/byteorder.h>"
//#endif
//    __u8	tos;
//    __be16	tot_len;
//    __be16	id;
//    __be16	frag_off;
//    __u8	ttl;
//    __u8	protocol;
//    __sum16	check;
//    __be32	saddr;
//    __be32	daddr;
//    /*The options start here. */
//};


//struct tcphdr {
//    __be16	source;
//    __be16	dest;
//    __be32	seq;
//    __be32	ack_seq;
//#if defined(__LITTLE_ENDIAN_BITFIELD)
//    __u16	res1:4,
//		doff:4,
//		fin:1,
//		syn:1,
//		rst:1,
//		psh:1,
//		ack:1,
//		urg:1,
//		ece:1,
//		cwr:1;
//#elif defined(__BIG_ENDIAN_BITFIELD)
//    __u16	doff:4,
//		res1:4,
//		cwr:1,
//		ece:1,
//		urg:1,
//		ack:1,
//		psh:1,
//		rst:1,
//		syn:1,
//		fin:1;
//#else
//#error	"Adjust your <asm/byteorder.h> defines"
//#endif
//    __be16	window;
//    __sum16	check;
//    __be16	urg_ptr;
//};

struct bpf_map_def SEC("maps") xsks_map = {
        .type = BPF_MAP_TYPE_XSKMAP,
        .key_size = sizeof(int),
        .value_size = sizeof(int),
        .max_entries = 4
};


SEC("xdp_sock")
int xdp_sock_prog(struct xdp_md *ctx)
{
    bpf_printk("get a packet, len %d", (ctx->data_end - ctx->data));
    char* data = (char*)(long)ctx->data;
    char* data_end = (char*)(long)ctx->data_end;
    if(data+sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct tcphdr) < data_end) {
        struct ethhdr* eth = (struct ethhdr*)data;
        struct iphdr* ip = (struct iphdr*)(eth+1);
        struct tcphdr* tcp = (struct tcphdr*)(ip+1);

        if(eth->h_proto != htons(ETH_P_IP)) {
            bpf_printk("pass a non ip packet, len %d", data_end - data);
            return XDP_PASS;
        }

        if(ip->protocol != IPPROTO_TCP) {
            bpf_printk("pass a non tcp packet, len %d", data_end - data);
            return XDP_PASS;
        }

        if(tcp->dest != htons(8080)) {
            bpf_printk("pass a non target 8080 port tcp packet, len %d", data_end - data);
            return XDP_PASS;
        }

        bpf_printk("redirect a packet");
        return bpf_redirect_map(&xsks_map, 0, 0);
    }

    bpf_printk("pass a packet, len %d", data_end - data);
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";