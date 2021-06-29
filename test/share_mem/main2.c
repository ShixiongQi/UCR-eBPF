// Linux does not support sharing of global variable through dynamic linked libraries
// We have to use shm_open/mmap to do it

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_SHARED_SIZE 40

static void __exit_with_error(const char* error, const char* file, const char* func, int line) {
    fprintf(stderr, "error: %s \n  at %s %d %s\n", error, file, line, func);
    exit(EXIT_FAILURE);
}

#define exit_with_error(error)  __exit_with_error(error, __FILE__, __func__, __LINE__)

static int should_exit = 0;

static void int_exit(int sig) {
	should_exit = 1;
}

void loop_read() {
    int fd = shm_open("myshare", O_RDWR, 0);
    if(fd < 0) {
        exit_with_error("shm_open failed");
    }

    int* data = mmap(NULL, DEFAULT_SHARED_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(data == MAP_FAILED) {
        exit_with_error("mmap failed");
    }

    while(!should_exit) {
        sleep(1);
        for(int i=0; i<10; i++) {
            printf("%5d", data[i]);
        }
        printf("\n");
    }
}

void loop_write() {
    int fd = shm_open("myshare", O_RDWR | O_CREAT, 0777);
    if(fd < 0) {
        exit_with_error("shm_open failed");
    }
    int ret = ftruncate(fd, DEFAULT_SHARED_SIZE);
    if(ret != 0) {
        exit_with_error("ftruncate failed");
    }

    int* data = mmap(NULL, DEFAULT_SHARED_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(data == MAP_FAILED) {
        exit_with_error("mmap failed");
    }

    int x = 0;
    while(!should_exit) {
        sleep(1);
        for(int i=0; i<10; i++) {
            data[i] = x++;
        }
    }
}


int main(int argc, char* argv[]) {
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    if(argc == 2 && strcmp("-w", argv[1]) == 0) {
        printf("create and write:\n");
        loop_write();
    }else {
        printf("read mode:\n");
        loop_read();
    }
}