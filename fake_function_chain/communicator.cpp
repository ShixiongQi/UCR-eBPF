#include "communicator.h"
#include "config.h"
#include "context.h"

namespace fc
{
    int CommunicatorClient::get_segment_id()
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::GET_SEGMENT_ID;
        int ret = send(fd, buffer, sizeof(u32), MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret == 4);
        
        return *((int*)buffer);
    }


    int CommunicatorClient::get_af_xdp_fd()
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::GET_PID_AF_XDP_FD;
        int ret = send(fd, buffer, sizeof(u32), MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret == 8);

        pid_t pid = *((pid_t*)buffer);
        int remote_fd = *((int*)(buffer + sizeof(pid_t)));
        // printf("pid: %d remote_fd: %d\n", pid, remote_fd);
        int pidfd = syscall(SYS_pidfd_open, pid, 0);
        assert(pidfd != -1);

        int fd_out = syscall(__NR_pidfd_getfd, pidfd, remote_fd, 0);
        assert(fd_out != -1);

        return fd_out;
    }


    void CommunicatorClient::get_bpf_program_map_fd(int* out_prog_fd, int* out_map_fd)
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::GET_BPF_PROGRAM;
        int ret = send(fd, buffer, 4, MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret == 12);

        pid_t pid = *((pid_t*)buffer);
        int remote_prog_fd = *((int*)(buffer + 4));
        int remote_map_fd = *((int*)(buffer + 8));
        
        int pidfd = syscall(SYS_pidfd_open, pid, 0);
        assert(pidfd != -1);

        int prog_fd = syscall(__NR_pidfd_getfd, pidfd, remote_prog_fd, 0);
        assert(prog_fd != -1);

        int map_fd = syscall(__NR_pidfd_getfd, pidfd, remote_map_fd, 0);
        assert(map_fd != -1);

        *out_prog_fd = prog_fd;
        *out_map_fd = map_fd;
    }


    void CommunicatorClient::create()
    {
        fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        assert(fd >= 0);

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));

        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, CONFIG::SOCKET_PATH, sizeof(addr.sun_path) - 1);

        int ret = connect(fd, (const struct sockaddr*)(&addr), sizeof(addr));
        assert(ret != -1);
    }







    void CommunicatorServer::create()
    {
        server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        assert(server_fd >= 0);

        sockaddr_un server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, CONFIG::SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

        unlink(CONFIG::SOCKET_PATH);
        int ret = bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        assert(ret >= 0);

        ret = listen(server_fd, 100);
        assert(ret >= 0);
    }

    void CommunicatorServer::client_handler(int client_fd)
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];

        while(1)
        {
            int ret = recv(client_fd, buffer, sizeof(buffer), 0);
            if(ret == 0)
            {
                std::cout << "client socket closed\n";
                break;
            }
            assert(ret > 0);

            u32 request = *((u32*)buffer);
            assert(request >= 0);
            assert(request < (u32)RequestType::MAX);
            RequestType type = (RequestType)request;
            switch(type)
            {
                case RequestType::GET_SEGMENT_ID:
                {
                    *((int*)buffer) = gateway_context->segment_id;
                    ret = send(client_fd, buffer, sizeof(int), MSG_EOR);
                    assert(ret != -1);
                }
                break;
                case RequestType::GET_PID_AF_XDP_FD:
                {
                    pid_t pid = getpid();
                    *((pid_t*)buffer) = pid;
                    *((int*)(buffer + sizeof(pid_t))) = gateway_context->af_xdp_fd;
                    // printf("pid: %d  fd: %d\n", pid, umem->fd);
                    ret = send(client_fd, buffer, sizeof(pid_t) + sizeof(int), MSG_EOR);
                    assert(ret != -1);
                }
                    break;
                case RequestType::GET_BPF_PROGRAM:
                {
                    pid_t pid = getpid();
                    *((int*)(buffer)) = pid;
                    *((int*)(buffer + 4)) = gateway_context->prog_fd;
                    *((int*)(buffer + 8)) = gateway_context->map_fd;
                    ret = send(client_fd, buffer, 12, MSG_EOR);
                    assert(ret != -1);
                }
                    break;
                default:
                    printf("unknown request type: %d\n", request);
                    assert(0);
            }
        }
    }

    void CommunicatorServer::serve_client()
    {
        while(1)
        {
            int client_fd = accept(server_fd, NULL, NULL);
            assert(client_fd != -1);
            printf("new client\n");

            std::thread(&CommunicatorServer::client_handler, this, client_fd).detach();
        }

        if(server_fd > 0)
        {
            close(server_fd);
            server_fd = -1;
        }
    }
}