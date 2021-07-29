#pragma once

#include "shared_definitions.h"
#include "queue.h"

namespace xdp
{
    // class containing information for a xdp socket
    class Sock
    {
    public:
        // the rx ring
        xdp_queue rxr;
        // the tx ring
        xdp_queue txr;
        int fd;

        void create();

        void bind_to_device(int if_index, bool shared = false, int shared_fd = 0);
    };
}