#include "shared_includes.h"
#include "config.h"
#include "queue.h"
#include "util.h"
#include "communicator.h"
#include "context.h"


void handler(u8* pkt)
{
    printf("get a packet\n");
}

int main(int argc, char* argv[])
{
    using namespace fc;

    if(argc != 2) {
		printf("usage: function {interface_name}\n");
		return 0;
	}

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    CommunicatorClient client;
    client.create();

    FunctionContext ctx;
    ctx.communicator = &client;
    ctx.init(argv[1]);

    ctx.update_ebpf_map(1, ctx.af_xdp_fd);

    
    ctx.handleRequests(handler);

    return 0;
}
