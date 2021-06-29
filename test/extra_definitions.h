#pragma once

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL	69
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif