#pragma once
#include "shared_includes.h"
#include "config.h"
#include "queue.h"
#include "communicator.h"

namespace fc
{


    // class containing information for shared umem
    class Context
    {
    public:
        // the segment id of shared memory
        int segment_id;
        // the data region of umem
        u8* data;
        // the fd of AF_XDP
        int af_xdp_fd;
        // the fd of ebpf program
        int prog_fd;
        // the fd of ebpf map
        int map_fd;

        // the rx ring
        xdp_queue rxr;
        // the tx ring
        xdp_queue txr;

        // set size and get rx
        void get_rxr_txr();

        // get packet data from addr in descriptor of rx ring
        inline u8* get_data(u64 addr)
        {
            return &data[addr];
        }

        Context()
        {
            // create AF_XDP socket
            af_xdp_fd = socket(AF_XDP, SOCK_RAW, 0);
            assert(af_xdp_fd != -1);
        }

        int nametoindex(const char* if_name);

        void bind_to_device(const char* if_name, bool shared = false, int shared_fd = 0);

        // update the ebpf map
        void update_ebpf_map(int key, int value);

        void send(int frame);

        // pull the rx ring and get a frame
        int receive();
    };

    // return the length of the packet after processing
    typedef void (*cb_handler)(u8* pkt);

    class FunctionContext : public Context
    {
    public:
        ICommunicatorClient* communicator;
        void get_shm();

        void init(const char* if_name);

        void handleRequests(cb_handler handler);
    };

    
    typedef int (*cb_shouldRecycle)(u8* pkt);

    // this part is only for manager
    // only manager will control fill ring and completion ring
    class GatewayContext : public Context
    {
    public:
        // the fill ring
        umem_queue fr;
        // the completion ring
        umem_queue cr;
        
        // the index for next free frame position
        u32 next_idx = CONFIG::NUM_FRAMES + 1;
        u64 free_frames[CONFIG::NUM_FRAMES];

        // allocate shared memory
        void alloc_shm();
        // register shared memory
        void register_umem();

        // set size and get fill ring and completion ring
        void get_fr_cr();
        
        // continually enqueue fill ring until it is full
        void enq_fr_full();
        // continually dequeue completion ring in a loop and free the descriptors based on its fields
        void loop_deq_cr(cb_shouldRecycle shouldRecycle);

        // do initialization
        void init(const char* if_name);
    };

}