//
// Created by Ziteng Zeng.
//

#pragma once
#include "shared_includes.h"

extern void allow_unlimited_locking();

extern void int_exit(int sig);

extern int get_proj_id(const char* fc_name);

extern void printHex(u8* start, int len);