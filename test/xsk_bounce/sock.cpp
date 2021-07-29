#include "sock.h"
#include "config.h"

namespace xdp
{
    void Sock::create()
    {
        int rx_ring_size = CONFIG::RX_RING_SIZE;
        int tx_ring_size = CONFIG::TX_RING_SIZE;
        int ret = setsockopt(fd, SOL_XDP, XDP_RX_RING, &rx_ring_size, sizeof(int));
        assert(ret == 0);
        ret = setsockopt(fd, SOL_XDP, XDP_TX_RING, &tx_ring_size, sizeof(int));
        assert(ret == 0);

        struct xdp_mmap_offsets off;
        socklen_t optlen = sizeof(off);
        ret = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
        assert(ret == 0);
        
        // rx ring
        rxr.map = (u8*)(mmap(0, off.rx.desc + CONFIG::RX_RING_SIZE * sizeof(xdp_desc),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, fd,
                        XDP_PGOFF_RX_RING));
        assert(rxr.map != MAP_FAILED);

        rxr.mask = CONFIG::RX_RING_SIZE - 1;
        rxr.size = CONFIG::RX_RING_SIZE;
        rxr.producer = (u32*)(rxr.map + off.rx.producer);
        rxr.consumer = (u32*)(rxr.map + off.rx.consumer);
        rxr.ring = (xdp_desc*)(rxr.map + off.rx.desc);


        // tx ring
        txr.map = (u8*)(mmap(0, off.tx.desc + CONFIG::TX_RING_SIZE * sizeof(xdp_desc),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, fd,
                        XDP_PGOFF_TX_RING));
        assert(txr.map != MAP_FAILED);

        txr.mask = CONFIG::TX_RING_SIZE - 1;
        txr.size = CONFIG::TX_RING_SIZE;
        txr.producer = (u32*)(txr.map + off.tx.producer);
        txr.consumer = (u32*)(txr.map + off.tx.consumer);
        txr.ring = (xdp_desc*)(txr.map + off.tx.desc);
        txr.cached_cons = CONFIG::TX_RING_SIZE;
    }


    void Sock::bind_to_device(int if_index, bool shared, int shared_fd)
    {
        // bind shared umem
        sockaddr_xdp addr;
        addr.sxdp_family = AF_XDP;
        addr.sxdp_flags = shared ? XDP_SHARED_UMEM : 0;
        addr.sxdp_ifindex = if_index;
        addr.sxdp_queue_id = 0;
        addr.sxdp_shared_umem_fd = shared ? shared_fd : 0;

        int ret = bind(fd, (const sockaddr*)(&addr), sizeof(addr));
        assert(ret == 0);
    }

}