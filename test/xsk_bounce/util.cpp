#include "util.h"

namespace xdp
{
    void allow_unlimited_locking()
    {
        struct rlimit lim =
        {
            .rlim_cur = RLIM_INFINITY,
            .rlim_max = RLIM_INFINITY
        };

        int ret = setrlimit(RLIMIT_MEMLOCK, &lim);
        assert(ret == 0);
    }


    void int_exit(int sig)
    {
        exit(-1);
    }
}