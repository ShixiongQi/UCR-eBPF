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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <bpf/bpf.h>
#include "extra_definitions.h"


struct umem_info {
    void* buffer;
    u64 free_frames_array[DEFAULT_NUM_FRAMES];
    u32 free_frame_cnt;
};

static struct umem_info* umem;

static int should_exit = 0;

static void int_exit(int sig) {
	should_exit = 1;
}

static void* exit_checker(void* arguments) {
    while(!should_exit) {
        sleep(1);
    }
    exit(0);
    return 0;
}

static void create_umem() {
    umem = calloc(1, sizeof(struct umem_info));
    if(!umem) {
        exit_with_error("memory allocation failed");
    }
    umem->free_frame_cnt = DEFAULT_NUM_FRAMES;
    for(int i=0; i<DEFAULT_NUM_FRAMES; i++) {
        umem->free_frames_array[i] = i * DEFAULT_FRAME_SIZE;
    }
    int fd = shm_open(DEFAULT_UMEM_FILE_NAME, O_RDWR | O_CREAT, 0777);
    if(fd < 0) {
        exit_with_error("shm_open failed");
    }

    int ret = ftruncate(fd, DEFAULT_FRAME_SIZE * DEFAULT_NUM_FRAMES);
    if(ret != 0) {
        exit_with_error("ftruncate failed");
    }

    umem->buffer = mmap(NULL, DEFAULT_FRAME_SIZE * DEFAULT_NUM_FRAMES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(umem->buffer == MAP_FAILED) {
        exit_with_error("mmap failed");
    }
}

// allocate a umem frame, return the descriptor
static u64 alloc_frame() {
	if(umem->free_frame_cnt == 0) {
		exit_with_error("umem frame allocation failed");
	}

	u64 frame = umem->free_frames_array[--umem->free_frame_cnt];
	//printf("allocate %p\n", (void*)frame);
	return frame;
}


// free a umem frame.
static void free_frame(u64 frame) {
	// based on my experiment, it seems that rx ring does not always return aligned address
	frame = frame & (~(DEFAULT_FRAME_SIZE-1));
	if(umem->free_frame_cnt == DEFAULT_NUM_FRAMES) {
		exit_with_error("free more times than allocation times");
	}
	umem->free_frames_array[umem->free_frame_cnt++] = frame;
	//printf("free %p\n", (void*)frame);
}


// the thread function that will serve a client's api call
static void* serve_client(void* arguments) {
    int client_fd = (int)arguments;
    u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];
    while(1) {
        int ret = recv(client_fd, buffer, sizeof(buffer), MSG_WAITALL);
        if(ret == 0) {
            printf("client socket closed\n");
            break;
        }
        if(ret < 0) {
            exit_with_error("read failed");
        }

        u32 request = *((u32*)buffer);
        switch(request) {
            case REQUEST_ALLOC:
            {
                u64 frame = alloc_frame();
                *((u64*)buffer) = frame;
                ret = send(client_fd, buffer, sizeof(u64), MSG_EOR);
                if(ret == -1) {
                    exit_with_error("send failed");
                }
            }
                break;
            case REQUEST_DEALLOC:
            {
                u64 frame = *((u64*)(buffer + sizeof(u32)));
                free_frame(frame);
            }
                break;
            default:
                printf("unknown request type: %d\n", request);
                exit_with_error("unknown request type");
        }
        // buffer[ret] = '\0';
        // for(int i=0; i<ret; i++) {
        //     buffer[i] = (char)toupper(buffer[i]);
        // }

        // ret = send(client_fd, buffer, ret, MSG_EOR);
        // if(ret == -1) {
        //     exit_with_error("write failed");
        // }
    }

    close(client_fd);
    return 0;
}


int main(int argc, char* argv[]) {
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    create_umem();

    struct sockaddr_un server_addr;
    int server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(server_fd < 0) {
        exit_with_error("AF_UNIX socket creation failed");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    unlink(DEFAULT_SOCKET_PATH);
    int ret = bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret < 0) {
        exit_with_error("bind unix socket failed");
    }

    ret = listen(server_fd, 20);
    if(ret < 0) {
        exit_with_error("unix socket listen failed");
    }

    pthread_t th;
    ret = pthread_create(&th, NULL, exit_checker, NULL);
    if(ret != 0) {
        exit_with_error("create thread failed");
    }

    printf("waiting for connections\n");

    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if(client_fd == -1) {
            exit_with_error("accept failure");
        }
        printf("new client\n");
        ret = pthread_create(&th, NULL, serve_client, (void*)(long)client_fd);
        if(ret != 0) {
            exit_with_error("create thread failed");
        }
    }

    close(server_fd);
    unlink(DEFAULT_SOCKET_PATH);

    return 0;
}