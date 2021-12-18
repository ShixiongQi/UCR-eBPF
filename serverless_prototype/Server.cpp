//
// Created by Ziteng Zeng.
//

#include "Server.h"
#include "util.h"
#include "nanotime.h"

void Server::create(const char *fc_name, const char *if_name) {
    // create AF_XDP socket
    af_xdp_fd = socket(AF_XDP, SOCK_RAW, 0);
    assert(af_xdp_fd != -1);


    // create umem
    this->segment_id = shmget(get_proj_id(fc_name), CONFIG::UMEM_SIZE, IPC_CREAT | 0666);
    assert(segment_id != -1);
    printf("segment id is %d\n", segment_id);

    data = (u8*)shmat(segment_id, NULL, 0);
    assert(data != (u8*)-1);


    // init record for free frames
    for(int i=0; i<CONFIG::NUM_FRAMES; i++)
    {
        free_frames[i] = i * CONFIG::FRAME_BYTES;
    }


    // load program
    int ret;
    struct bpf_object* obj;

    struct bpf_prog_load_attr load_attr = {
            .file = "./rx_kern.o",
            .prog_type = BPF_PROG_TYPE_XDP
    };

    ret = bpf_prog_load_xattr(&load_attr, &obj, &xdp_prog_fd);
    assert(ret == 0);

    int if_index = nametoindex(if_name);
    ret = bpf_set_link_xdp_fd(if_index, xdp_prog_fd, XDP_FLAGS_DRV_MODE);
    assert(ret >= 0);

    xdp_map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
    assert(xdp_map_fd >= 0);


    // register umem
    xdp_umem_reg reg;
    reg.addr = (u64)data;
    reg.len = CONFIG::UMEM_SIZE;
    reg.chunk_size = CONFIG::FRAME_BYTES;
    reg.headroom = CONFIG::HEADROOM_SIZE;
    reg.flags = CONFIG::UMEM_FLAGS;

    ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
    assert(ret == 0);


    // get rx ring and tx ring
    int rx_ring_size = CONFIG::RX_RING_SIZE;
    int tx_ring_size = CONFIG::TX_RING_SIZE;
    ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_RX_RING, &rx_ring_size, sizeof(int));
    assert(ret == 0);
    ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_TX_RING, &tx_ring_size, sizeof(int));
    assert(ret == 0);

    struct xdp_mmap_offsets off;
    socklen_t optlen = sizeof(off);
    ret = getsockopt(af_xdp_fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
    assert(ret == 0);

    // rx ring
    rxr.map = (u8*)(mmap(0, off.rx.desc + CONFIG::RX_RING_SIZE * sizeof(xdp_desc),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, af_xdp_fd,
                         XDP_PGOFF_RX_RING));
    assert(rxr.map != MAP_FAILED);

    rxr.mask = CONFIG::RX_RING_SIZE - 1;
    rxr.size = CONFIG::RX_RING_SIZE;
    rxr.producer = (u32*)(rxr.map + off.rx.producer);
    rxr.consumer = (u32*)(rxr.map + off.rx.consumer);
    rxr.ring = (xdp_desc*)(rxr.map + off.rx.desc);
    rxr.cached_prod = *rxr.producer;
    rxr.cached_cons = *rxr.consumer;

    // tx ring
    txr.map = (u8*)(mmap(0, off.tx.desc + CONFIG::TX_RING_SIZE * sizeof(xdp_desc),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, af_xdp_fd,
                         XDP_PGOFF_TX_RING));
    assert(txr.map != MAP_FAILED);

    txr.mask = CONFIG::TX_RING_SIZE - 1;
    txr.size = CONFIG::TX_RING_SIZE;
    txr.producer = (u32*)(txr.map + off.tx.producer);
    txr.consumer = (u32*)(txr.map + off.tx.consumer);
    txr.ring = (xdp_desc*)(txr.map + off.tx.desc);
    txr.cached_prod = *txr.producer;
    txr.cached_cons = *txr.consumer;


    // get fill ring and completion ring
    int fr_size = CONFIG::FILL_RING_SIZE;
    int cr_size = CONFIG::COMPLETION_RING_SIZE;

    ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_FILL_RING, &fr_size, sizeof(int));
    assert(ret == 0);

    ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cr_size, sizeof(int));
    assert(ret == 0);

    optlen = sizeof(off);
    ret = getsockopt(af_xdp_fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
    assert(ret == 0);

    // fill ring
    fr.map = (u8*)mmap(0, off.fr.desc + CONFIG::FILL_RING_SIZE * sizeof(u64),
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED |MAP_POPULATE,
                       af_xdp_fd, XDP_UMEM_PGOFF_FILL_RING);
    assert(fr.map != MAP_FAILED);

    fr.mask = CONFIG::FILL_RING_SIZE - 1;
    fr.size = CONFIG::FILL_RING_SIZE;
    fr.producer = (u32*)(fr.map + off.fr.producer);
    fr.consumer = (u32*)(fr.map + off.fr.consumer);
    fr.ring = (u64*)(fr.map + off.fr.desc);
    fr.cached_prod = *fr.producer;
    fr.cached_cons = *fr.consumer;

    // completion ring
    cr.map = (u8*)mmap(0, off.cr.desc + CONFIG::COMPLETION_RING_SIZE * sizeof(u64),
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE,
                       af_xdp_fd, XDP_UMEM_PGOFF_COMPLETION_RING);
    assert(cr.map != MAP_FAILED);

    cr.mask = CONFIG::COMPLETION_RING_SIZE - 1;
    cr.size = CONFIG::COMPLETION_RING_SIZE;
    cr.producer = (u32*)(cr.map + off.cr.producer);
    cr.consumer = (u32*)(cr.map + off.cr.consumer);
    cr.ring = (u64*)(cr.map + off.cr.desc);
    cr.cached_prod = *cr.producer;
    cr.cached_cons = *cr.consumer;


    // bind to device
    sockaddr_xdp xdp_addr;
    xdp_addr.sxdp_family = AF_XDP;
    xdp_addr.sxdp_flags = 0;
    xdp_addr.sxdp_ifindex = if_index;
    xdp_addr.sxdp_queue_id = 0;
    xdp_addr.sxdp_shared_umem_fd = 0;

    ret = bind(af_xdp_fd, (const sockaddr*)(&xdp_addr), sizeof(xdp_addr));
    assert(ret == 0);


    // enqueue fill ring to full one time
    int free_entries = fr.nb_free();
    assert(free_entries == CONFIG::FILL_RING_SIZE);

    assert(next_idx >= CONFIG::FILL_RING_SIZE);

    next_idx -= CONFIG::FILL_RING_SIZE;
    ret = fr.enq(&free_frames[next_idx], CONFIG::FILL_RING_SIZE);
    assert(ret == CONFIG::FILL_RING_SIZE);

    // update ebpf map
    int key = 0;
    ret = bpf_map_update_elem(xdp_map_fd, &key, &af_xdp_fd, 0);
    assert(ret == 0);


    // load program
    ret = bpf_prog_load("./sk_msg_kern.o", BPF_PROG_TYPE_SK_MSG, &obj, &skmsg_prog_fd);
    assert(ret == 0);

    // get map fd
    skmsg_map_fd = bpf_object__find_map_fd_by_name(obj, "sock_map");
    assert(skmsg_map_fd >= 0);

    // attach map
    ret = bpf_prog_attach(skmsg_prog_fd, skmsg_map_fd, BPF_SK_MSG_VERDICT, 0);
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

    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ret = connect(skmsg_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
    assert(ret == 0);

    key = 0;
    ret = bpf_map_update_elem(skmsg_map_fd, &key, &skmsg_socket_fd, 0);
    assert(ret == 0);
}

void Server::add_rpc(rpc::server *server) {
    server->bind(GET_SHM_SEGMENT_ID, [this](){return this->segment_id;});
    server->bind(UPDATE_SOCKMAP, [this](int fun_pid, int fun_sk_msg_sock_fd, int key){
        int pidfd = syscall(SYS_pidfd_open, fun_pid, 0);
        assert(pidfd != -1);

        int sock_fd = syscall(__NR_pidfd_getfd, pidfd, fun_sk_msg_sock_fd, 0);
        assert(sock_fd != -1);

        int ret = bpf_map_update_elem(this->skmsg_map_fd, &key, &sock_fd, 0);
        assert(ret == 0);

        printf("sockmap pos %d updated\n", key);
    });
}

int Server::nametoindex(const char *if_name) {
    int if_index = if_nametoindex(if_name);
    assert(if_index != 0);
    return if_index;
}

u64 Server::receive() {
    while(true)
    {
        u32 entries_available = rxr.nb_avail();
        if(entries_available == 0) {
            continue;
        }

        xdp_desc desc;
        rxr.deq(&desc, 1);
        printf("receive frame %llu\n", desc.addr);
        return desc.addr;
    }
}

void Server::run_async() {
    // create metadata and send to function 1
    std::thread t([this](){
        u64 frame = receive();
        Meta meta;
        meta.next = 1;
        meta.timestamp = get_time_nano();
        meta.frame = frame;
        int ret = send(skmsg_socket_fd, &meta, sizeof(meta), 0);
        assert(ret == sizeof(meta));
    });

    t.detach();

    // send http response back to the user through tx ring
    std::thread t2([this](){
        while(true) {
            Meta meta;
            int ret = recv(skmsg_socket_fd, &meta, sizeof(meta), 0);
            assert(ret == sizeof(meta));

            printf("send http response packet back to the user(not implemented yet)\n");
        }
    });

    t2.detach();

    // recycle frame on completion ring
    std::thread t3([this](){
        while(true) {
            int avail_entries = cr.nb_avail();
            if(avail_entries == 0)
            {
                continue;
            }
            assert(avail_entries >= 1);

            // avail_entries >= 1
            u64 frame;
            int ret = cr.deq(&frame, 1);
            assert(ret == 1);

            assert(next_idx != CONFIG::NUM_FRAMES);
            free_frames[next_idx++] = get_aligned_frame(frame);
        }
    });

    t3.detach();
}