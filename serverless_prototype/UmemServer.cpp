//
// Created by Ziteng Zeng.
//

#include "UmemServer.h"
#include "util.h"
#include "config.h"

void UmemServer::create(const char* fc_name, const char* if_name) {
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

    ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
    assert(ret == 0);

    int if_index = nametoindex(if_name);
    ret = bpf_set_link_xdp_fd(if_index, prog_fd, XDP_FLAGS_DRV_MODE);
    assert(ret >= 0);

    map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
    assert(map_fd >= 0);


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
    sockaddr_xdp addr;
    addr.sxdp_family = AF_XDP;
    addr.sxdp_flags = 0;
    addr.sxdp_ifindex = if_index;
    addr.sxdp_queue_id = 0;
    addr.sxdp_shared_umem_fd = 0;

    ret = bind(af_xdp_fd, (const sockaddr*)(&addr), sizeof(addr));
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
    ret = bpf_map_update_elem(map_fd, &key, &af_xdp_fd, 0);
    assert(ret == 0);
}

void UmemServer::add_rpc(rpc::server *server) {
    server->bind(GET_SHM_SEGMENT_ID, [this](){return this->segment_id;});
}


int UmemServer::nametoindex(const char *if_name) {
    int if_index = if_nametoindex(if_name);
    assert(if_index != 0);
    return if_index;
}

u64 UmemServer::receive() {
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

void UmemServer::run_dispacher(std::function<void(u64)> callback) {
    std::thread t([=](){
        u64 frame = receive();
        callback(frame);
    });

    t.detach();
}


