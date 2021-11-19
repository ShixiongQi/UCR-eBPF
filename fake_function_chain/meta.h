#pragma once
#include "shared_includes.h"

namespace fc
{
    struct Meta
    {
        struct ethhdr eth;
        // 16
        int magic;
        u32 next_function;
        u32 tag;

        u32 input_pkt_start;
        u32 input_pkt_len;

        u32 output_pkt_start;
        u32 output_pkt_len;
    };
}