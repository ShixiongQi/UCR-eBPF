//
// Created by Ziteng Zeng.
//

#pragma once
#include <rpc/server.h>

class SkmsgServer {
public:
    int prog_fd;
    int sockmap_fd;

    int dummy_server_socket_fd;
    int skmsg_socket_fd;

    void create();

    void add_rpc(rpc::server* srv);
};



