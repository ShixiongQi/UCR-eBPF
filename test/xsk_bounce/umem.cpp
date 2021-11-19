#include "umem.h"
#include "config.h"


namespace xdp
{
    void Umem::map_memory()
    {
        int mem_fd = shm_open(CONFIG::UMEM_FILE_NAME, O_RDWR, 0);
        assert(mem_fd >= 0);

        frames = (u8*)mmap(nullptr, CONFIG::UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
        assert(frames != MAP_FAILED);
    }






    FrameAllocator::FrameAllocator()
    {
        // initialize index
        for(int i=0; i < CONFIG::NUM_FRAMES; i++)
        {
            free_frames[i] = i * CONFIG::FRAME_SIZE;
        }
    }

    u64 FrameAllocator::alloc_frame()
    {
        std::lock_guard<std::mutex> guard(mtx);
        assert(next_idx != 0);

        u64 frame = free_frames[--next_idx];
        //printf("allocate %p\n", (void*)frame);
        return frame;
    }

    void FrameAllocator::free_frame(u64 frame)
    {
        std::lock_guard<std::mutex> guard(mtx);

        // based on my experiment, it seems that rx ring does not always return aligned address
        frame = frame & (~(CONFIG::FRAME_SIZE-1));
        assert(next_idx < CONFIG::NUM_FRAMES + 1);

        free_frames[next_idx++] = frame;
        //printf("free %p\n", (void*)frame);
    }






    void UmemManager::map_memory()
    {
        int mem_fd = shm_open(CONFIG::UMEM_FILE_NAME, O_RDWR | O_CREAT, 0777);
        assert(mem_fd >= 0);

        int ret = ftruncate(mem_fd, CONFIG::UMEM_SIZE);
        assert(ret == 0);

        // no need to worry about aligning
        // on Linux, the mapping will be created at a nearby page boundary
        frames = (u8*)mmap(nullptr, CONFIG::UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
        assert(frames != MAP_FAILED);
    }


    void UmemManager::register_umem()
    {
        xdp_umem_reg reg;
        reg.addr = (u64)frames;
        reg.len = CONFIG::UMEM_SIZE;
        reg.chunk_size = CONFIG::FRAME_SIZE;
        reg.headroom = CONFIG::HEADROOM_SIZE;
        reg.flags = 0;

        int ret = setsockopt(fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
        assert(ret == 0);
    }

    void UmemManager::set_fr_cr_size()
    {
        int fr_size = CONFIG::FILL_RING_SIZE;
        int cr_size = CONFIG::COMPLETION_RING_SIZE;

        int ret = setsockopt(fd, SOL_XDP, XDP_UMEM_FILL_RING, &fr_size, sizeof(int));
        assert(ret == 0);

        ret = setsockopt(fd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cr_size, sizeof(int));
        assert(ret == 0);
    }

    void UmemManager::get_fr_cr()
    {
        xdp_mmap_offsets off;
        socklen_t optlen = sizeof(off);
        int ret = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
        assert(ret == 0);
        
        // fill ring
        fr.map = (u8*)mmap(0, off.fr.desc + CONFIG::FILL_RING_SIZE * sizeof(u64),
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED |MAP_POPULATE,
                            fd, XDP_UMEM_PGOFF_FILL_RING);
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
                            fd, XDP_UMEM_PGOFF_COMPLETION_RING);
        assert(cr.map != MAP_FAILED);

        cr.mask = CONFIG::COMPLETION_RING_SIZE - 1;
        cr.size = CONFIG::COMPLETION_RING_SIZE;
        cr.producer = (u32*)(cr.map + off.cr.producer);
        cr.consumer = (u32*)(cr.map + off.cr.consumer);
        cr.ring = (u64*)(cr.map + off.cr.desc);
    }


    void UmemManager::create()
    {
        // since a socket descriptor is needed for creating umem, a dummy AF_XDP socket is created
        fd = socket(AF_XDP, SOCK_RAW, 0);
        assert(fd != -1);

        map_memory();
        register_umem();
        set_fr_cr_size();
        get_fr_cr();
    }


    void UmemManager::loop_enq_fr()
    {
        while(1)
        {
            int free_entries = fr.nb_free();
            if(free_entries == 0)
            {
                continue;
            }
            assert(free_entries > 0);

            // free_entries > 0
            u64 frame = allocator.alloc_frame();
            size_t ret = fr.enq(&frame, 1);
            assert(ret == 1);
        }
    }

    void UmemManager::loop_deq_cr()
    {
        while(1) 
        {
            int avail_entries = cr.nb_avail();
            if(avail_entries == 0) 
            {
                continue;
            }
            assert(avail_entries == 1);

            printf("[completion ring] consumer: %d producer: %d\n", *cr.consumer, *cr.producer);
            // avail_entries == 1
            u64 frame;
            int ret = cr.deq(&frame, 1);
            assert(ret == 1);

            allocator.free_frame(frame);
        }
    }
}