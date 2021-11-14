#pragma once
#include "shared_includes.h"

namespace fc 
{
    namespace CONFIG 
    {
        // the size of a single frame
        constexpr int FRAME_BYTES = 4096;
        // the size of a packet
        constexpr int PKT_BYTES = 20;
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

        constexpr int HEADROOM_SIZE = 0;
        constexpr int UMEM_FLAGS = 0;

        constexpr const char* SOCKET_PATH = "/tmp/fc.socket";
        constexpr int SOCKET_MAX_BUFFER_SIZE = 32;

        constexpr int BPF_MAP_MAX_ENTRIES = 1024;

        constexpr const char* FC_NAME = "webserver";
    }

    // based on my experiment, it seems that rx ring does not always return aligned address
    inline u64 get_aligned_frame(u64 frame)
    {
        return frame & (~(CONFIG::FRAME_BYTES-1));
    }
}