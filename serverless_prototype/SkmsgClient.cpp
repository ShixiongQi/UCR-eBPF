//
// Created by Ziteng Zeng.
//

#include "SkmsgClient.h"
#include "config.h"
#include "nanotime.h"

void SkmsgClient::create(rpc::client* client, int fun_id, const char* gateway_ip) {
     // create skmsg socket
     skmsg_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
     assert(skmsg_socket_fd != -1);

     // connect to gateway
     struct sockaddr_in addr;
     addr.sin_family = AF_INET;
     addr.sin_port = htons(CONFIG::SERVER_SK_MSG_TCP_PORT);
     addr.sin_addr.s_addr = inet_addr(gateway_ip);

     int ret = connect(skmsg_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
     assert(ret == 0);

     client->call(UPDATE_SOCKMAP, (int)getpid(), skmsg_socket_fd, fun_id);
}

void SkmsgClient::run() {
    while(true) {
        int buf[6];
        int ret = recv(skmsg_socket_fd, &buf, 24, 0);
        assert(ret == 24);

        printf("received data frame: %lu elapsed time nano: %ld\n", *(u64*)&buf[4], get_time_nano() - *(long*)(&buf[2]));
    }
}
