//
// Created by Ziteng Zeng.
//

#include "Client.h"
#include "config.h"
#include "nanotime.h"

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>

void Client::create(rpc::client *client, int fun_id) {
    int segment_id = client->call(GET_SHM_SEGMENT_ID).as<int>();
    assert(segment_id != -1);
    printf("segment id is %d\n", segment_id);

    data = (u8*)shmat(segment_id, NULL, 0);
    assert(data != (u8*)-1);

    // create skmsg socket
    skmsg_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(skmsg_socket_fd != -1);

    // connect to gateway
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONFIG::SERVER_SK_MSG_TCP_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int ret = connect(skmsg_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    client->call(UPDATE_SOCKMAP, (int)getpid(), skmsg_socket_fd, fun_id);
}

void Client::run(std::function<int(u8*, int)> callback) {
    while(true) {
        Meta meta;
        int ret = recv(skmsg_socket_fd, &meta, sizeof(meta), 0);
        assert(ret == sizeof(meta));

        u8* ptr = &data[meta.frame];
        struct ethhdr* eth = (struct ethhdr*)ptr;
        struct iphdr* ip = (struct iphdr*)(eth+1);
        struct tcphdr* tcp = (struct tcphdr*)(ip+1);
        u8* data = (u8*)(tcp+1);
        int len = ntohs(ip->tot_len) - ip->ihl * 4 - tcp->doff * 4;

        Meta meta_send;
        meta_send.next = callback(data, len);
        meta_send.timestamp = get_time_nano();
        meta_send.frame = meta.frame;
        ret = send(skmsg_socket_fd, &meta_send, sizeof(meta_send), 0);
        assert(ret == sizeof(meta));
    }
}