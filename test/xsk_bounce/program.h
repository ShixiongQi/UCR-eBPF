#pragma once
#include "shared_definitions.h"


namespace xdp
{
    // class containing information for ebpf kernel program
    class Program
    {
    public:
        int prog_fd;
        int map_fd;

        void load(const char* path, int if_index);

        void update_map(int key, int value);
    };
}