//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"
#include <rpc/server.h>
#include "queue.h"
#include "config.h"

class UmemServer {
public:
    // the segment id of shared memory
    int segment_id;
    // the data region of umem
    u8* data;

public:
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

    // the fill ring
    umem_queue fr;
    // the completion ring
    umem_queue cr;

    // the index for next free frame position
    u32 next_idx = CONFIG::NUM_FRAMES;
    u64 free_frames[CONFIG::NUM_FRAMES];

public:
    void create(const char* fc_name, const char* if_name);
    void add_rpc(rpc::server* server);
    int nametoindex(const char* if_name);
    // pull the rx ring and get a frame
    u64 receive();
    // get packet data from addr in descriptor of rx ring
    inline u8* get_data(u64 addr)
    {
        return &data[addr];
    }
    // create a thread and run dispatcher in it
    void run_dispacher(std::function<void(u64)> callback);
};



