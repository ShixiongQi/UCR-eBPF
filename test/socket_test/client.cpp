#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <time.h>
#include <unistd.h>


namespace xdp_time
{
	static void get_monotonic_time(struct timespec* ts) {
		clock_gettime(CLOCK_MONOTONIC, ts);
	}

	static long get_time_nano(struct timespec* ts) {
		return (long)ts->tv_sec * 1e9 + ts->tv_nsec;
	}

	static long get_time_nano()
	{
		timespec ts;
		get_monotonic_time(&ts);
		return get_time_nano(&ts);
	}

	static double get_elapsed_time_sec(struct timespec* before, struct timespec* after) {
		double deltat_s  = after->tv_sec - before->tv_sec;
		double deltat_ns = after->tv_nsec - before->tv_nsec;
		return deltat_s + deltat_ns*1e-9;
	}

	static long get_elapsed_time_nano(struct timespec* before, struct timespec* after) {
		return get_time_nano(after) - get_time_nano(before);
	}
}


int main(int argc, char* argv[])
{
	if(argc != 3)
	{
		printf("usage: client {server_ip} {buffer_size}\n");
		return 0;
	}
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	int buffer_size = atoi(argv[2]);
	assert(buffer_size > 0 && buffer_size <= 2048);

	sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    int ret = inet_aton(argv[1], &addr.sin_addr);
	assert(ret != 0);

    char buf[2048];
    while(true)
    {
		printf("time sent: %ld\n", xdp_time::get_time_nano());
        ret = sendto(sock, buf, buffer_size, 0, (sockaddr*)&addr, sizeof(addr));
		assert(ret != -1);

		sleep(1);
    }

    return 0;
}
