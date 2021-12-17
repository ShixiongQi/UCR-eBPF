//
// Created by Ziteng Zeng.
//

#include "shared_includes.h"
#include <bits/stdc++.h>
#include <rpc/client.h>
#include "config.h"
#include "UmemClient.h"
#include "SkmsgClient.h"

using std::string;

int main() {
    rpc::client client("10.0.0.2", CONFIG::SERVER_RPC_PORT);

    UmemClient umemClient;
    umemClient.create(&client);

    int gateway_pid = client.call(GET_GATEWAY_PID).as<int>();
    printf("gateway_pid = %d\n", gateway_pid);

    int function_id = client.call(ALLOCATE_FUNCTION_ID).as<int>();
    printf("function id is %d\n", function_id);

    SkmsgClient skmsgClient;
    skmsgClient.create(&client, function_id, "10.0.0.2");

    skmsgClient.run();

    return 0;
}