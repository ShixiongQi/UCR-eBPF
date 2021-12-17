//
// Created by Ziteng Zeng.
//

#include "shared_includes.h"
#include <bits/stdc++.h>
#include <rpc/server.h>
#include "config.h"
#include "UmemServer.h"
#include "SkmsgServer.h"
#include "util.h"
#include "nanotime.h"

int next_id = 1;

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
    srv.bind(ALLOCATE_FUNCTION_ID, [&](){return next_id++;});

    UmemServer umemServer;
    umemServer.create(fc_name, if_name);
    umemServer.add_rpc(&srv);

    SkmsgServer skmsgServer;
    skmsgServer.create();
    skmsgServer.add_rpc(&srv);

    umemServer.run_dispacher([&](u64 frame){
        int buf[6];
        buf[0] = 1;
        *(long*)(&buf[2]) = get_time_nano();
        int ret = send(skmsgServer.skmsg_socket_fd, &buf, sizeof(buf), 0);
        assert(ret != -1);
    });

    srv.run();

    return 0;
}