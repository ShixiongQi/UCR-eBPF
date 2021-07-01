#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <bpf/bpf.h>
#include "extra_definitions.h"

static int should_exit = 0;

static void int_exit(int sig) {
	should_exit = 1;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    struct sockaddr_un addr;
    int client_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(client_fd < 0) {
        exit_with_error("AF_UNIX socket creation failed");
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    int ret = connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr));
    if(ret == -1) {
        exit_with_error("cannot connect to manager");
    }


    u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];
    
    for(int i=0; i<3; i++) {
        *((u32*)buffer) = REQUEST_ALLOC;
        ret = send(client_fd, buffer, sizeof(u32), MSG_EOR);
        printf("request allocation\n");
        if(ret == -1) {
            exit_with_error("send failed");
        }

        ret = recv(client_fd, buffer, sizeof(buffer), MSG_WAITALL);
        if(ret == -1) {
            exit_with_error("recvmsg failed");
        }else if(ret == 0) {
            exit_with_error("no data to read");
        }

        // ensure buffer is 0-terminated
        u64 frame = *((u64*)buffer);
        printf("get frame %llu\n", frame);
    }

    close(client_fd);

    return 0;
}