#pragma once

#include "shared_includes.h"

namespace fc
{
    extern void allow_unlimited_locking();

    extern void int_exit(int sig);

    extern int get_proj_id();

    extern void printHex(u8* start, int len);
}