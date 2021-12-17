//
// Created by Ziteng Zeng.
//

#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

/* user accessible metadata for SK_MSG packet hook, new fields must
 * be added to the end of this structure
 */
//struct sk_msg_md {
//    __bpf_md_ptr(void *, data);
//    __bpf_md_ptr(void *, data_end);
//
//    __u32 family;
//    __u32 remote_ip4;	/* Stored in network byte order */
//    __u32 local_ip4;	/* Stored in network byte order */
//    __u32 remote_ip6[4];	/* Stored in network byte order */
//    __u32 local_ip6[4];	/* Stored in network byte order */
//    __u32 remote_port;	/* Stored in network byte order */
//    __u32 local_port;	/* stored in host byte order */
//    __u32 size;		/* Total size of sk_msg */
//
//    __bpf_md_ptr(struct bpf_sock *, sk); /* current socket */
//};


struct bpf_map_def SEC("maps") sock_map = {
        .type = BPF_MAP_TYPE_SOCKMAP,
        .key_size = sizeof(int),
        .value_size = sizeof(int),
        .max_entries = 16,
        .map_flags = 0
};


SEC("sk_msg")
int bpf_tcpip_bypass(struct sk_msg_md *msg)
{
    char* data = msg->data;
    char* data_end = msg->data_end;
    bpf_printk("[sk_msg] get a packet of length %d", msg->size);

    if(data + 4 > data_end) {
        return SK_DROP;
    }
    int key = *((int*)data);
    bpf_printk("[sk_msg] redirect to socket at array position %d", key);

    int ret = bpf_msg_redirect_map(msg, &sock_map, key, BPF_F_INGRESS);
    if(ret == SK_PASS) {
        //bpf_printk("[sk_msg] redirect success");
    }else if(ret == SK_DROP) {
        //bpf_printk("[sk_msg] redirect error");
    }else{
        //bpf_printk("[sk_msg] unknown redirect result");
    }
    return ret;
}

char _license[] SEC("license") = "GPL";