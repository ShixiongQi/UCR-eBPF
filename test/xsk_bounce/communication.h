#pragma once
#include "shared_definitions.h"
#include "umem.h"
#include "program.h"

namespace xdp
{
    enum class RequestType : u32 
    {
        // In: none Out: u64
        ALLOC,
        // In: u64 Out: none
        DEALLOC,
        // In: none Out: int(pid_t) and int
        GET_PID_FD,
        // In: none Out: pid(int) program_fd(int) map_fd(int)
        GET_BPF_PROGRAM,
        MAX
    };


    class Socket
    {
    public:
        int fd = -1;

        void close();
        ~Socket();
        // create the socket and set fd to according socket file descriptor
        virtual void create() = 0;

        operator int() const { return fd; }
    };

    class ListeningServer : public Socket
    {
    public:
        virtual void create() override;
    };


    class Server : public Socket
    {
    public:
        virtual void create() override;

        // the function that will serve a client's api call
        void serve_client(UmemManager* umem, Program* program);
    };


    class Client : public Socket
    {
    public:
        virtual void create() override;

        int get_manager_xsk_fd();
        void get_manager_xdp_prog_fd(int* out_prog_fd, int* out_map_fd);

        void free_frame(u64 frame);
        u64 alloc_frame();
    };


}