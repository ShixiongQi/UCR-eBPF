#include "util.h"
#include "config.h"

namespace fc
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

    int get_proj_id()
    {
        char* start = (char*)CONFIG::FC_NAME;

        int hash = 0;
        while(*start != '\0') {
            hash = hash * 1007 + *start;
            hash = hash % 1000007;
            start++;
        }

        return hash;
    }

    char getHexDigit(int n) {
        if(n<10) {
            return '0' + n;
        }else{
            return 'a' + n - 10;
        }
    }

    void printHex(u8* start, int len)
    {
        for(int i=0; i<len; i++) {
            u8 x = start[i];
            putchar(getHexDigit(x/16));
            putchar(getHexDigit(x%16));
            putchar(' ');
        }
        putchar('\n');
    }
}