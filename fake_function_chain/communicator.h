#pragma once
#include "shared_includes.h"
#include "config.h"


namespace fc
{
    enum class RequestType : u32 
    {
        GET_SEGMENT_ID,
        // In: none Out: int(pid_t) and int
        GET_PID_AF_XDP_FD,
        // In: none Out: pid(int) program_fd(int) map_fd(int)
        GET_BPF_PROGRAM,
        MAX
    };

    class ICommunicatorClient
    {
    public:
        virtual int get_segment_id() = 0;
        virtual int get_af_xdp_fd() = 0;
        virtual void get_bpf_program_map_fd(int* prog_fd, int* map_fd) = 0;
    };

    class CommunicatorClient : public ICommunicatorClient
    {
    public:
        int fd;
        int get_segment_id();
        int get_af_xdp_fd();
        void get_bpf_program_map_fd(int* prog_fd, int* map_fd);

        void create();
    };


    class GatewayContext;
    class CommunicatorServer
    {
    public:
        int server_fd = -1;
        class GatewayContext* gateway_context;

        void create();

        void serve_client();

        void client_handler(int client_fd);
    };
}