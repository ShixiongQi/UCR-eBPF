//
// Created by Ziteng Zeng.
//

#include "shared_includes.h"
#include "SkmsgServer.h"
#include "config.h"

void SkmsgServer::create() {
    // load program
    struct bpf_object *obj;
    int ret = bpf_prog_load("./sk_msg_kern.o", BPF_PROG_TYPE_SK_MSG, &obj, &prog_fd);
    assert(ret == 0);

    // get map fd
    sockmap_fd = bpf_object__find_map_fd_by_name(obj, "sock_map");
    assert(sockmap_fd >= 0);

    // attach map
    ret = bpf_prog_attach(prog_fd, sockmap_fd, BPF_SK_MSG_VERDICT, 0);
    assert(ret == 0);

    // create dummy server
    dummy_server_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(dummy_server_socket_fd != -1);

    int one = 1;
    ret = setsockopt(dummy_server_socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
    assert(ret == 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONFIG::SERVER_SK_MSG_TCP_PORT);
    addr.sin_addr.s_addr = inet_addr("0.0.0.0");

    ret = bind(dummy_server_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    ret = listen(dummy_server_socket_fd, 100);
    assert(ret == 0);

    std::thread t([this](){
        while(true) {
            int connection_socket = accept(this->dummy_server_socket_fd, NULL, NULL);
            assert(connection_socket != -1);
        }
    });
    t.detach();


    // create skmsg socket
    skmsg_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(skmsg_socket_fd != -1);

    addr.sin_addr.s_addr = inet_addr("10.0.0.2");
    ret = connect(skmsg_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    int key = 0;
    ret = bpf_map_update_elem(sockmap_fd, &key, &skmsg_socket_fd, 0);
    assert(ret == 0);
}

void SkmsgServer::add_rpc(rpc::server *srv) {
    srv->bind(UPDATE_SOCKMAP, [this](int fun_pid, int fun_sk_msg_sock_fd, int key){
        int pidfd = syscall(SYS_pidfd_open, fun_pid, 0);
        assert(pidfd != -1);

        int sock_fd = syscall(__NR_pidfd_getfd, pidfd, fun_sk_msg_sock_fd, 0);
        assert(sock_fd != -1);

        int ret = bpf_map_update_elem(this->sockmap_fd, &key, &sock_fd, 0);
        assert(ret == 0);

        printf("sockmap pos %d updated\n", key);
    });
}
