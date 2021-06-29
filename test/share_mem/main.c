#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>

static void __exit_with_error(const char* error, const char* file, const char* func, int line) {
    fprintf(stderr, "error: %s \n  at %s %d %s\n", error, file, line, func);
    exit(EXIT_FAILURE);
}

#define exit_with_error(error)  __exit_with_error(error, __FILE__, __func__, __LINE__)


static int should_exit = 0;
static int* data_size;
static int* data;

static void int_exit(int sig) {
	should_exit = 1;
}

void loop_read() {
	while(!should_exit) {
		sleep(1);
		for(int i=0; i<*data_size; i++) {
			printf("%5d", data[i]);
		}
		printf("\n");
	}
}

void loop_write() {
	int x = 0;
	while(!should_exit) {
		sleep(1);
		for(int i=0; i<*data_size; i++) {
			data[i] = x++;
		}
	}
}

int main(int argc, char* argv[]) {
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	void* lib_handle = dlopen("./libshare.so", RTLD_LAZY);
	if(!lib_handle) {
		exit_with_error("can not load library");
	}
	data_size = dlsym(lib_handle, "data_size");
	data = dlsym(lib_handle, "data");
	if(data_size == 0 || data == 0) {
		exit_with_error("can not get symbol");
	}

	printf("data_size: %d\n", *data_size);

	if(argc == 2 && strcmp("-w", argv[1]) == 0) {
		printf("write mode:\n");
		loop_write();
	}else {
		printf("read mode:\n");
		loop_read();
	}
}