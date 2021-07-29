#include "communication.h"
#include "config.h"

namespace xdp
{
    void Socket::close()
    {
        if(fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }


    Socket::~Socket()
    {
        close();
    }


    void ListeningServer::create()
    {
        sockaddr_un server_addr;
        fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        assert(fd >= 0);

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, CONFIG::SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

        unlink(CONFIG::SOCKET_PATH);
        int ret = bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        assert(ret >= 0);

        ret = listen(fd, 20);
        assert(ret >= 0);
    }


    void Server::create()
    {

    }


    void Server::serve_client(UmemManager* umem, Program* program)
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];

        while(1)
        {
            int ret = recv(fd, buffer, sizeof(buffer), 0);
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
                case RequestType::ALLOC:
                {
                    u64 frame = umem->allocator.alloc_frame();
                    *((u64*)buffer) = frame;
                    ret = send(fd, buffer, sizeof(u64), MSG_EOR);
                    assert(ret != -1);
                }
                    break;
                case RequestType::DEALLOC:
                {
                    u64 frame = *((u64*)(buffer + sizeof(u32)));
                    umem->allocator.free_frame(frame);
                }
                    break;
                case RequestType::GET_PID_FD:
                {
                    pid_t pid = getpid();
                    *((pid_t*)buffer) = pid;
                    *((int*)(buffer + sizeof(pid_t))) = umem->fd;
                    // printf("pid: %d  fd: %d\n", pid, umem->fd);
                    ret = send(fd, buffer, sizeof(pid_t) + sizeof(int), MSG_EOR);
                    assert(ret != -1);
                }
                    break;
                case RequestType::GET_BPF_PROGRAM:
                {
                    pid_t pid = getpid();
                    *((int*)(buffer)) = pid;
                    *((int*)(buffer + 4)) = program->prog_fd;
                    *((int*)(buffer + 8)) = program->map_fd;
                    ret = send(fd, buffer, 12, MSG_EOR);
                    assert(ret != -1);
                }
                    break;
                default:
                    printf("unknown request type: %d\n", request);
                    assert(0);
            }
        }

        close();
    }












    

    void Client::create()
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


    int Client::get_manager_xsk_fd()
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::GET_PID_FD;
        int ret = send(fd, buffer, sizeof(u32), MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret != -1);
        assert(ret != 0);

        pid_t pid = *((pid_t*)buffer);
        int remote_fd = *((int*)(buffer + sizeof(pid_t)));
        // printf("pid: %d remote_fd: %d\n", pid, remote_fd);
        int pidfd = syscall(SYS_pidfd_open, pid, 0);
        assert(pidfd != -1);

        int fd_out = syscall(__NR_pidfd_getfd, pidfd, remote_fd, 0);
        assert(fd_out != -1);

        return fd_out;
    }


    void Client::get_manager_xdp_prog_fd(int* out_prog_fd, int* out_map_fd)
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::GET_BPF_PROGRAM;
        int ret = send(fd, buffer, 4, MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret != -1);
        assert(ret != 0);

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


    void Client::free_frame(u64 frame)
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::DEALLOC;
        *((u64*)(buffer + 4)) = frame;
        int ret = send(fd, buffer, 12, MSG_EOR);
        assert(ret != -1);
    }



    u64 Client::alloc_frame()
    {
        u8 buffer[CONFIG::SOCKET_MAX_BUFFER_SIZE];
        *((u32*)buffer) = (u32)RequestType::ALLOC;
        int ret = send(fd, buffer, 4, MSG_EOR);
        assert(ret != -1);
        ret = recv(fd, buffer, sizeof(buffer), 0);
        assert(ret != -1);
        assert(ret != 0);

        return *((u64*)buffer);
    }
}