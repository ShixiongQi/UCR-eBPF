//
// Created by Ziteng Zeng.
//

#include "shared_includes.h"
#include <bits/stdc++.h>
#include <rpc/client.h>
#include "config.h"
#include "util.h"
#include "Client.h"

using std::string;

int main(int argc, char* argv[]) {
    if(argc != 2) {
        printf("Usage: FunMain {function_id}\n");
        return 0;
    }
    // parse argument
    int function_id = atoi(argv[1]);

    signal(SIGINT, int_exit);
    signal(SIGTERM, int_exit);
    rpc::client rpcClient("127.0.0.1", CONFIG::SERVER_RPC_PORT);

    Client client;
    client.create(&rpcClient, function_id);
    client.run([](u8* ptr, int len){
        printf("ptr: %p, len: %d\n", ptr, len);
        for(int i=0; i<len; i++) {
            putchar(ptr[i]);
        }

        return 0;
    });

    return 0;
}