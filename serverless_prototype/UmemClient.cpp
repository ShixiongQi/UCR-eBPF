//
// Created by Ziteng Zeng.
//

#include "UmemClient.h"
#include "config.h"

void UmemClient::create(rpc::client* client) {
    int segment_id = client->call(GET_SHM_SEGMENT_ID).as<int>();
    assert(segment_id != -1);
    printf("segment id is %d\n", segment_id);

    data = (u8*)shmat(segment_id, NULL, 0);
    assert(data != (u8*)-1);
}
