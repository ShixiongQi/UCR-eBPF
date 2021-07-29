#include "shared_definitions.h"
#include "config.h"
#include "queue.h"
#include "sock.h"
#include "umem.h"
#include "util.h"
#include "communication.h"
#include "program.h"


int main(int argc, char* argv[])
{
    using namespace xdp;

    if(argc != 2) {
		printf("usage: manager {interface_name}\n");
		return 0;
	}
    int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);
    // printf("if_index: %d\n", if_index);

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    Program prog;
	prog.load("./rx_kern.o", if_index);

    UmemManager* mem = new UmemManager();
    mem->create();

    Sock* s = new Sock();
    s->fd = mem->fd;
    s->create();
    s->bind_to_device(if_index);

    std::thread(&UmemManager::loop_enq_fr, mem).detach();
    std::thread(&UmemManager::loop_deq_cr, mem).detach();

    ListeningServer listenServer;
    listenServer.create();

    std::cout << "waiting for connections\n";
    while(1)
    {
        int client_fd = accept(listenServer, NULL, NULL);
        assert(client_fd != -1);
        std::cout << "new client\n";

        Server* server = new Server();
        server->fd = client_fd;
        server->create();

        std::thread(&Server::serve_client, server, mem, &prog).detach();
    }

    listenServer.close();

    return 0;
}