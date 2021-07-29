#pragma once
#include "shared_definitions.h"
#include "config.h"
#include "queue.h"

namespace xdp
{

    // class containing information for shared umem
    class Umem
    {
    public:
        u8* frames;
        int fd;
        // get packet data from addr in descriptor of rx ring
        inline u8* get_data(u64 addr)
        {
            return &frames[addr];
        }

        // map shared memory
        virtual void map_memory();
    };


    class FrameAllocator
    {
    public:
        std::mutex mtx;
        // the index for next free frame position
        u32 next_idx = CONFIG::NUM_FRAMES + 1;
        u64 free_frames[CONFIG::NUM_FRAMES];

        FrameAllocator();

        // allocate a umem frame, return the descriptor
        u64 alloc_frame();

        // free a umem frame.
        void free_frame(u64 frame);
    };


    
    // this part is only for manager
    // only manager will control fill ring and completion ring
    class UmemManager : public Umem
    {
    public:
        // the fill ring
        umem_queue fr;
        // the completion ring
        umem_queue cr;
        FrameAllocator allocator;

        virtual void map_memory() override;

        void register_umem();

        // set size for fill ring and completion ring
        void set_fr_cr_size();

        // get fill ring and completion ring
        void get_fr_cr();

        void create();
        
        // continually enqueue fill ring in a loop
        void loop_enq_fr();
        // continually dequeue completion ring in a loop and free the descriptors
        void loop_deq_cr();
    };

}