//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"
#include <rpc/client.h>
#include "config.h"

class Client {
public:
    // the data region of umem
    u8* data;

public:
    int skmsg_socket_fd;

public:
    void create(rpc::client* client, int fun_id);

    void run(std::function<int(u8*, int)> callback);
};



