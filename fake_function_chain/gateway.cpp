#include "shared_includes.h"
#include "config.h"
#include "queue.h"
#include "util.h"
#include "communicator.h"
#include "context.h"
#include "meta.h"
#include "nanotime.h"

using fc::Meta;

int main(int argc, char* argv[])
{
    using namespace fc;

    if(argc != 2) {
		printf("usage: gateway {interface_name}\n");
		return 0;
	}

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    GatewayContext ctx;
    ctx.init(argv[1]);
    
    CommunicatorServer server;
    server.gateway_context = &ctx;
    server.create();

    std::thread(&CommunicatorServer::serve_client, &server).detach();
    
    int tag = 1;
    while(true)
    {
        u64 frame = ctx.receive();
        u64 aligned_frame = frame - fc::CONFIG::HEADROOM_SIZE;
        u8* data = ctx.get_data(aligned_frame);
        
        struct Meta* meta = (struct Meta*)data;

        meta->magic = CONFIG::magic;
        meta->next_function = 1;
        meta->tag = tag++;

        meta->eth.h_dest[0] = 0x7a;
        meta->eth.h_dest[1] = 0x74;
        meta->eth.h_dest[2] = 0x54;
        meta->eth.h_dest[3] = 0x65;
        meta->eth.h_dest[4] = 0x6a;
        meta->eth.h_dest[5] = 0xdb;

        meta->eth.h_source[0] = 0x32;
        meta->eth.h_source[1] = 0xf4;
        meta->eth.h_source[2] = 0x94;
        meta->eth.h_source[3] = 0x15;
        meta->eth.h_source[4] = 0x5b;
        meta->eth.h_source[5] = 0x83;

        meta->eth.h_proto = 0;

        ctx.send(aligned_frame);

        printHex(data, 50);
        printf("send\n");
        printf("[completion ring] consumer: %d producer: %d\n", *ctx.cr.consumer, *ctx.cr.producer);
    }

    return 0;
}
