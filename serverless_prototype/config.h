//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"

namespace CONFIG
{
    // the size of a single frame
    constexpr int FRAME_BYTES = 4096;
    // the size of fill ring
    constexpr int FILL_RING_SIZE = 1024;
    // the size of completion ring
    constexpr int COMPLETION_RING_SIZE = 1024;
    // the size of rx ring
    constexpr int RX_RING_SIZE = 1024;
    // the size of tx ring
    constexpr int TX_RING_SIZE = 1024;
    // the number of frames
    constexpr int NUM_FRAMES = 16 * 1024;
    // the total size of umem
    constexpr int UMEM_SIZE = NUM_FRAMES * FRAME_BYTES;

    constexpr int HEADROOM_SIZE = 20;
    constexpr int UMEM_FLAGS = 0;

    constexpr int SERVER_RPC_PORT = 8080;
    constexpr int SERVER_SK_MSG_TCP_PORT = 5556;
}

// int ()
#define GET_SHM_SEGMENT_ID "get_shm_segment_id"
// int ()
#define GET_GATEWAY_PID "get_gateway_pid"
// int ()
#define ALLOCATE_FUNCTION_ID "allocate_function_id"
// void (int fun_pid, int fun_sk_msg_sock_fd, int key)
#define UPDATE_SOCKMAP "update_sk_msg_sockmap"


// based on my experiment, it seems that rx ring does not always return aligned address
inline u64 get_aligned_frame(u64 frame)
{
    return frame & (~(CONFIG::FRAME_BYTES-1));
}