//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"
#include <rpc/client.h>

class SkmsgClient {
public:
    int skmsg_socket_fd;

public:
    void create(rpc::client* client, int fun_id, const char* gateway_ip);

    void run();
};



