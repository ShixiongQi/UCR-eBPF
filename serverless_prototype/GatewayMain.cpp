//
// Created by Ziteng Zeng.
//

#include "shared_includes.h"
#include <bits/stdc++.h>
#include <rpc/server.h>
#include "config.h"
#include "Server.h"
#include "util.h"

int main(int argc, char* argv[]) {
    if(argc != 3) {
        printf("Usage: GatewayMain {fc_name} {net_interface_name}\n");
        return 0;
    }
    // parse argument
    char* fc_name = argv[1];
    char* if_name = argv[2];

    allow_unlimited_locking();
    signal(SIGINT, int_exit);
    signal(SIGTERM, int_exit);

    // add rpc server handlers
    rpc::server srv(CONFIG::SERVER_RPC_PORT);
    srv.bind(GET_GATEWAY_PID, [](){return (int)getpid();});

    Server server;
    server.create(fc_name, if_name);
    server.add_rpc(&srv);
    server.run_async();

    srv.run();

    return 0;
}