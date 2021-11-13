#include "shared_includes.h"
#include "config.h"
#include "queue.h"
#include "util.h"
#include "communicator.h"
#include "context.h"


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
    

    while(true)
    {
        sleep(1);
        ctx.next_idx--;
        ctx.send(ctx.free_frames[ctx.next_idx]);
        printf("send\n");
    }


    // Program prog;
	// prog.load("./rx_kern.o", if_index);

    // UmemManager* mem = new UmemManager();
    // mem->create();

    // Sock* s = new Sock();
    // s->fd = mem->fd;
    // s->create();
    // s->bind_to_device(if_index);

    // std::thread(&UmemManager::loop_enq_fr, mem).detach();
    // std::thread(&UmemManager::loop_deq_cr, mem).detach();

    // ListeningServer listenServer;
    // listenServer.create();

    // std::cout << "waiting for connections\n";
    // while(1)
    // {
    //     int client_fd = accept(listenServer, NULL, NULL);
    //     assert(client_fd != -1);
    //     std::cout << "new client\n";

    //     Server* server = new Server();
    //     server->fd = client_fd;
    //     server->create();

    //     std::thread(&Server::serve_client, server, mem, &prog).detach();
    // }

    // listenServer.close();

    return 0;
}
