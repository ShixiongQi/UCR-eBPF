#include "shared_includes.h"
#include "config.h"
#include "queue.h"
#include "util.h"
#include "communicator.h"
#include "context.h"
#include "meta.h"
#include "nanotime.h"

using fc::Meta;

int func_id;

void handler(u8* pkt)
{
    struct Meta* meta = (struct Meta*)pkt;
    fc::printHex(pkt, 50);
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

    meta->next_function++;
    fc::printHex(pkt, 50);
}

int main(int argc, char* argv[])
{
    using namespace fc;

    if(argc != 3) {
		printf("usage: function {interface_name} {function_id}\n");
		return 0;
	}

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    CommunicatorClient client;
    client.create();

    int function_id = atoi(argv[2]);
    func_id = function_id;
    printf("function id: %d\n", function_id);

    FunctionContext ctx;
    ctx.communicator = &client;
    ctx.init(argv[1], function_id);

    printf("ready to handle request\n");
    ctx.handleRequests(handler);

    return 0;
}
