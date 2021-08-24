#include "shared_definitions.h"
#include "config.h"
#include "queue.h"
#include "sock.h"
#include "umem.h"
#include "util.h"
#include "communication.h"
#include "program.h"
#include "nanotime.h"

using namespace xdp;

// xdp map index that the current process is working on
static int g_cur_index = 0;
static bool g_last = false;

#pragma pack(push, 1)
struct timestamp
{
	u64 recv;
	u64 send;
};

struct frame_pkt
{
	int cnt;
	timestamp nano_timestamps[];
};

struct desc_pkt
{
	int dest_index;
	u64 frame;
};
#pragma pack(pop)

static Umem* g_mem;
static Sock* g_xsk;
static Client* g_client;
extern "C" void client_send()
{
	Umem* mem = g_mem;
    Sock* xsk = g_xsk;
    Client* client = g_client;

    printf("send packet to map index%d\n", g_cur_index + 1);
    
    u32 entries = xsk->txr.nb_free();
    if(entries == 0)
    {
        return;
    }

    // generate packet
    u64 frame = client->alloc_frame();
    frame_pkt* frame_start = (frame_pkt*)mem->get_data(frame);
    frame_start->cnt = 0;
    frame_start->nano_timestamps[frame_start->cnt].recv = get_time_nano();
    // frame_start->nano_timestamps[frame_start->cnt].send = get_time_nano();
    // frame_start->cnt++;

    // send packet metadata
    u64 desc_frame = client->alloc_frame();
    desc_pkt* desc_frame_start = (desc_pkt*)mem->get_data(desc_frame);
    desc_frame_start->dest_index = g_cur_index + 1;
    desc_frame_start->frame = frame;

    
    frame_start->nano_timestamps[frame_start->cnt].send = get_time_nano();
    frame_start->cnt++;

    xdp_desc desc;
    desc.addr = desc_frame;
    desc.len = sizeof(desc_pkt);
    xsk->txr.enq(&desc, 1);

    
    sendto(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
}

static void loop_receive(Umem* mem, Sock* xsk, Client* client) 
{
	while(true) 
	{
		xdp_desc desc;
		u32 entries_available = xsk->rxr.nb_avail();
		if(entries_available == 0) 
		{
			continue;
		}
		xsk->rxr.deq(&desc, 1);

		desc_pkt* desc_frame_start = (desc_pkt*)mem->get_data(desc.addr);
		frame_pkt* frame_start = (frame_pkt*)mem->get_data(desc_frame_start->frame);

		frame_start->nano_timestamps[frame_start->cnt].recv = get_time_nano();
		// extra processing logic can be here
		// ....
		frame_start->nano_timestamps[frame_start->cnt].send = get_time_nano();
		frame_start->cnt++;

		if(!g_last)
		{
			desc_frame_start->dest_index++;
			xsk->txr.enq(&desc, 1);

			sendto(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
		}
		else
		{
			timestamp& ts = frame_start->nano_timestamps[0];
			printf("recv: %lu  send: %lu\n", ts.recv, ts.send);

			for(int i=1; i<frame_start->cnt; i++)
			{
				timestamp& last_ts = frame_start->nano_timestamps[i-1];
				timestamp& ts = frame_start->nano_timestamps[i];
				printf("recv: %lu  send: %lu  delta: %lu\n", ts.recv, ts.send, ts.recv - last_ts.send);
			}

			client->free_frame(desc_frame_start->frame);
			client->free_frame(desc.addr);
		}
	}
}


// int main(int argc, char* argv[]) {
// 	if(argc < 4) {
// 		printf("usage: client {interface_name} {send/recv} [bpf_key] --last\n");
// 		return 0;
// 	}
// 	int if_index = if_nametoindex(argv[1]);
// 	assert(if_index != 0);


//     signal(SIGINT, int_exit);
// 	signal(SIGTERM, int_exit);

//     allow_unlimited_locking();

//     Client client;
// 	client.create();
	
// 	Program program;
// 	client.get_manager_xdp_prog_fd(&program.prog_fd, &program.map_fd);

// 	Umem* mem = new Umem();
// 	mem->map_memory();
// 	mem->fd = client.get_manager_xsk_fd();

// 	Sock* sock = new Sock();
// 	sock->fd = socket(AF_XDP, SOCK_RAW, 0);
// 	assert(sock->fd != -1);
// 	sock->create();
// 	sock->bind_to_device(if_index, true, mem->fd);


// 	g_cur_index = atoi(argv[3]);
// 	printf("set xdp socket at map index %d\n", g_cur_index);
// 	program.update_map(g_cur_index, sock->fd);

// 	if(argc >= 5 && strcmp("--last", argv[4]) == 0)
// 	{
// 		g_last = true;
// 		printf("this is the last endpoint in chain\n");
// 	}

// 	if(strcmp(argv[2], "send") == 0)
// 	{
// 		loop_send(mem, sock, &client);
// 	}
// 	else if(strcmp(argv[2], "recv") == 0) 
// 	{
// 		loop_receive(mem, sock, &client);
// 	}

// 	return 0;
// }


extern "C" int client_init(char* interface_name, int bpf_key) 
{
	int if_index = if_nametoindex(interface_name);
	assert(if_index != 0);


    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    Client* client = new Client();
	client->create();
	
	Program program;
	client->get_manager_xdp_prog_fd(&program.prog_fd, &program.map_fd);

	Umem* mem = new Umem();
	mem->map_memory();
	mem->fd = client->get_manager_xsk_fd();

	Sock* sock = new Sock();
	sock->fd = socket(AF_XDP, SOCK_RAW, 0);
	assert(sock->fd != -1);
	sock->create();
	sock->bind_to_device(if_index, true, mem->fd);


	printf("set xdp socket at map index %d\n", bpf_key);
	program.update_map(bpf_key, sock->fd);

    g_mem = mem;
    g_xsk = sock;
    g_client = client;

	return 0;
}