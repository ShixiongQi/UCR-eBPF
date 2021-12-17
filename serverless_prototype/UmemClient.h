//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"
#include <rpc/client.h>

class UmemClient {
public:
    // the data region of umem
    u8* data;

public:
    void create(rpc::client* client);
};



