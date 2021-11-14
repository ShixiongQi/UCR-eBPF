#include "config.h"
#include "context.h"
#include "util.h"


namespace fc
{
    int Context::nametoindex(const char* if_name)
    {
        int if_index = if_nametoindex(if_name);
        assert(if_index != 0);
        return if_index;
    }

    void Context::bind_to_device(const char* if_name, bool shared, int shared_fd)
    {
        int if_index = nametoindex(if_name);

        // bind shared umem
        sockaddr_xdp addr;
        addr.sxdp_family = AF_XDP;
        addr.sxdp_flags = shared ? XDP_SHARED_UMEM : 0;
        addr.sxdp_ifindex = if_index;
        addr.sxdp_queue_id = 0;
        addr.sxdp_shared_umem_fd = shared ? shared_fd : 0;

        int ret = bind(af_xdp_fd, (const sockaddr*)(&addr), sizeof(addr));
        assert(ret == 0);
    }


    void Context::get_rxr_txr()
    {
        int rx_ring_size = CONFIG::RX_RING_SIZE;
        int tx_ring_size = CONFIG::TX_RING_SIZE;
        int ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_RX_RING, &rx_ring_size, sizeof(int));
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
    }

    void Context::update_ebpf_map(int key, int value)
    {
        int ret = bpf_map_update_elem(map_fd, &key, &value, 0);
        assert(ret == 0);
    }

    void Context::send(int frame)
    {
        xdp_desc desc;
        desc.addr = frame;
        desc.len = CONFIG::PKT_BYTES;
        txr.enq(&desc, 1);
        printf("consumer: %d producer: %d\n", *txr.consumer, *txr.producer);
        sendto(af_xdp_fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
        printf("consumer: %d producer: %d\n", *txr.consumer, *txr.producer);


    }

    int Context::receive()
    {
        while(true)
        {
            u32 entries_available = rxr.nb_avail();
            //printf("prod: %u consumer %u available %d \n", rxr.cached_prod, rxr.cached_cons, entries_available);
            if(entries_available == 0) {
                continue;
            }

            xdp_desc desc;
            rxr.deq(&desc, 1);
            return desc.addr;
        }
    }
    





    void FunctionContext::get_shm()
    {
        segment_id = communicator->get_segment_id();
        assert(segment_id != -1);
        printf("segment id is %d\n", segment_id);

        data = (u8*)shmat(segment_id, NULL, 0);
        assert(data != (u8*)-1);
    }

    void FunctionContext::init(const char* if_name)
    {
        get_shm();
        communicator->get_bpf_program_map_fd(&prog_fd, &map_fd);

        get_rxr_txr();

        bind_to_device(if_name, true, communicator->get_af_xdp_fd());
    }


    void FunctionContext::handleRequests(cb_handler handler)
    {
        while(true)
        {
            int frame = receive();
            u8* pkt = get_data(frame);

            handler(pkt);

            //send(frame);
        }
    }






    void GatewayContext::alloc_shm()
    {
        segment_id = shmget(get_proj_id(), CONFIG::UMEM_SIZE, IPC_CREAT | 0666);
        assert(segment_id != -1);
        printf("segment id is %d\n", segment_id);

        data = (u8*)shmat(segment_id, NULL, 0);
        assert(data != (u8*)-1);
    }

    void GatewayContext::register_umem()
    {
        xdp_umem_reg reg;
        reg.addr = (u64)data;
        reg.len = CONFIG::UMEM_SIZE;
        reg.chunk_size = CONFIG::FRAME_BYTES;
        reg.headroom = CONFIG::HEADROOM_SIZE;
        reg.flags = 0;

        int ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
        assert(ret == 0);
    }

    void GatewayContext::get_fr_cr()
    {
        int fr_size = CONFIG::FILL_RING_SIZE;
        int cr_size = CONFIG::COMPLETION_RING_SIZE;

        int ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_FILL_RING, &fr_size, sizeof(int));
        assert(ret == 0);

        ret = setsockopt(af_xdp_fd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cr_size, sizeof(int));
        assert(ret == 0);

        xdp_mmap_offsets off;
        socklen_t optlen = sizeof(off);
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
        fr.cached_cons = CONFIG::FILL_RING_SIZE;

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
    }

    void GatewayContext::enq_fr_full()
    {
        int free_entries = fr.nb_free();
        assert(free_entries == CONFIG::FILL_RING_SIZE);

        assert(next_idx >= CONFIG::FILL_RING_SIZE);

        next_idx -= CONFIG::FILL_RING_SIZE;
        size_t ret = fr.enq(&free_frames[next_idx], CONFIG::FILL_RING_SIZE);
        assert(ret == CONFIG::FILL_RING_SIZE);
    }

    void GatewayContext::loop_deq_cr(cb_shouldRecycle shouldRecycle)
    {
        while(1) 
        {
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

            if(shouldRecycle(get_data(frame))) {
                assert(next_idx < CONFIG::NUM_FRAMES + 1);
                free_frames[next_idx++] = get_aligned_frame(frame);
            }
        }
    }

    


    void GatewayContext::init(const char* if_name)
    {
        for(int i=0; i<CONFIG::NUM_FRAMES; i++)
        {
            free_frames[i] = i * CONFIG::FRAME_BYTES;
        }

        int ret;
        // load program
        struct bpf_object* obj;

        struct bpf_prog_load_attr load_attr = {
            .file = "./rx_kern.o",
            .prog_type = BPF_PROG_TYPE_XDP
        };

        ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
        assert(ret == 0);

        ret = bpf_set_link_xdp_fd(nametoindex(if_name), prog_fd, XDP_FLAGS_DRV_MODE);
        assert(ret >= 0);
        
        map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
        assert(map_fd >= 0);

        // setup shared memory and rings
        alloc_shm();
        register_umem();
        get_fr_cr();
        enq_fr_full();
        get_rxr_txr();

        bind_to_device(if_name);

        update_ebpf_map(0, af_xdp_fd);
    }
}