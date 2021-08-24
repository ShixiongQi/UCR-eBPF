#pragma once
#include "shared_definitions.h"

namespace xdp
{
    namespace CONFIG
    {
        // the size of a single frame
        constexpr int FRAME_SIZE = 4096;
        // the number of frames
        constexpr int NUM_FRAMES = 16*1024;
        // the size of fill ring
        constexpr int FILL_RING_SIZE = 1024;
        // the size of completion ring
        constexpr int COMPLETION_RING_SIZE = 1024;
        // the size of rx ring
        constexpr int RX_RING_SIZE = 1024;
        // the size of tx ring
        constexpr int TX_RING_SIZE = 1024;
        // the total size of umem
        constexpr int UMEM_SIZE = NUM_FRAMES * FRAME_SIZE;
        constexpr const char* UMEM_FILE_NAME = "share_umem";
        constexpr int HEADROOM_SIZE = 0;
        constexpr int UMEM_FLAGS = 0;

        constexpr const char* SOCKET_PATH = "/tmp/share_umem.socket";
        constexpr int SOCKET_MAX_BUFFER_SIZE = 32;

        constexpr int BPF_MAP_MAX_ENTRIES = 1024;
    }
}