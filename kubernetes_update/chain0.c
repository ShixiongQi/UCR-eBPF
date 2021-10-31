#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

int compute_key(char* chain_name) {
    int key = 0;
    int i=0;
    while(chain_name[i]) {
        key = (key*107 + chain_name[i]) % 1000039;
        i++;
    }
    return key;
}

int create_shm(int key) {
    int segment = shmget(key, 4096, IPC_CREAT | 0666);
    assert(segment != -1);
    return segment;
}

int main(int argc, char* argv[]) {
    char* chain_name = getenv("functionChainName");
    if(chain_name == NULL) {
        printf("no environment variable functionChainName specified\n");
        return -1;
    }

    int key = compute_key(chain_name);
    printf("key is %d\n", key);

    int segment = create_shm(key);

    void* mem = shmat(segment, NULL, 0);
    assert(mem != (void*)-1);

    int i;
    for(i=0; i<100; i++) {
        ((int*)mem)[0] = i;
        printf("%d\n", i);
        sleep(1);
    }
    

    return 0;
}