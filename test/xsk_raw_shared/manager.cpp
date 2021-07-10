#include "shared_definitions.hpp"

// this part is only for manager
// only manager will control fill ring and completion ring
struct xdp_umem : xdp_umem_shared{
	// the fill ring
	struct xdp_umem_queue fr;
	// the completion ring
	struct xdp_umem_queue cr;
    std::mutex mtx;
    // the index for next free frame position
    u32 next_idx;
    u64 free_frames[DEFAULT_NUM_FRAMES];

	void create_shared_memory() {
		int mem_fd = shm_open(DEFAULT_UMEM_FILE_NAME, O_RDWR | O_CREAT, 0777);
		assert(mem_fd >= 0);

		int ret = ftruncate(mem_fd, DEFAULT_UMEM_SIZE);
    	assert(ret == 0);

		// no need to worry about aligning
    	// on Linux, the mapping will be created at a nearby page boundary
		frames = (u8*)mmap(nullptr, DEFAULT_UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
		assert(frames != MAP_FAILED);
	}

	void register_umem() {
		xdp_umem_reg reg;
		reg.addr = (u64)frames;
		reg.len = DEFAULT_UMEM_SIZE;
		reg.chunk_size = DEFAULT_FRAME_SIZE;
		reg.headroom = DEFAULT_HEADROOM;
		reg.flags = 0;

		int ret = setsockopt(fd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
		assert(ret == 0);
	}

	// set size for fill ring and completion ring
	void set_fr_cr_size() {
		int fr_size = DEFAULT_FILL_RING_SIZE;
		int cr_size = DEFAULT_COMPLETION_RING_SIZE;

		int ret = setsockopt(fd, SOL_XDP, XDP_UMEM_FILL_RING, &fr_size, sizeof(int));
		assert(ret == 0);

		ret = setsockopt(fd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cr_size, sizeof(int));
		assert(ret == 0);
	}

	// get fill ring and completion ring
	void get_fr_cr() {
		xdp_mmap_offsets off;
		socklen_t optlen = sizeof(off);
		int ret = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
		assert(ret == 0);
		
		// fill ring
		fr.map = (u8*)mmap(0, off.fr.desc + DEFAULT_FILL_RING_SIZE * sizeof(u64),
							PROT_READ | PROT_WRITE,
							MAP_SHARED |MAP_POPULATE,
							fd, XDP_UMEM_PGOFF_FILL_RING);
		assert(fr.map != MAP_FAILED);

		fr.mask = DEFAULT_FILL_RING_SIZE - 1;
		fr.size = DEFAULT_FILL_RING_SIZE;
		fr.producer = reinterpret_cast<u32*>(fr.map + off.fr.producer);
		fr.consumer = reinterpret_cast<u32*>(fr.map + off.fr.consumer);
		fr.ring = reinterpret_cast<u64*>(fr.map + off.fr.desc);
		fr.cached_cons = DEFAULT_FILL_RING_SIZE;

		// completion ring
		cr.map = (u8*)mmap(0, off.cr.desc + DEFAULT_COMPLETION_RING_SIZE * sizeof(u64),
							PROT_READ | PROT_WRITE,
							MAP_SHARED | MAP_POPULATE,
							fd, XDP_UMEM_PGOFF_COMPLETION_RING);
		assert(cr.map != MAP_FAILED);

		cr.mask = DEFAULT_COMPLETION_RING_SIZE - 1;
		cr.size = DEFAULT_COMPLETION_RING_SIZE;
		cr.producer = reinterpret_cast<u32*>(cr.map + off.cr.producer);
		cr.consumer = reinterpret_cast<u32*>(cr.map + off.cr.consumer);
		cr.ring = reinterpret_cast<u64*>(cr.map + off.cr.desc);
	}

    // allocate a umem frame, return the descriptor
    u64 alloc_frame() {
        mtx.lock();
        assert(next_idx != 0);

        u64 frame = free_frames[--next_idx];
        //printf("allocate %p\n", (void*)frame);
        mtx.unlock();
        return frame;
    }

    // free a umem frame.
    void free_frame(u64 frame) {
        mtx.lock();
        // based on my experiment, it seems that rx ring does not always return aligned address
        frame = frame & (~(DEFAULT_FRAME_SIZE-1));
        assert(next_idx < DEFAULT_NUM_FRAMES + 1);

        free_frames[next_idx++] = frame;
        //printf("free %p\n", (void*)frame);
        mtx.unlock();
    }

    void create() {
		// initialize index
		next_idx = DEFAULT_NUM_FRAMES + 1;
		for(int i=0; i < DEFAULT_NUM_FRAMES; i++) {
			free_frames[i] = i * DEFAULT_FRAME_SIZE;
		}

        // since a socket descriptor is needed for creating umem, a dummy AF_XDP socket is created
        fd = socket(AF_XDP, SOCK_RAW, 0);
        assert(fd != -1);

        create_shared_memory();
        register_umem();
        set_fr_cr_size();
        get_fr_cr();
	}
};


// continually enqueue fill ring in a loop
static void loop_enq_fr(xdp_umem* umem) {
    xdp_umem_queue *fr = &umem->fr;
    while(1) {
        int free_entries = fr->fr_nb_free(1);
        if(free_entries == 0) {
            continue;
        }
        assert(free_entries > 0);

        // free_entries > 0
        u64 frame = umem->alloc_frame();
        size_t ret = fr->fr_enq(&frame, 1);
        assert(ret == 1);
    }
}

// continually dequeue completion ring in a loop and free the descriptors
static void loop_deq_cr(xdp_umem* umem) {
    struct xdp_umem_queue *cr = &umem->cr;

    while(1) {
        int avail_entries = cr->cr_nb_avail(1);
        if(avail_entries == 0) {
            continue;
        }
        assert(avail_entries == 1);

        // avail_entries == 1
        u64 frame;
        int ret = cr->cr_deq(&frame, 1);
        assert(ret == 1);

        umem->free_frame(frame);
    }
}

// the thread function that will serve a client's api call
static void serve_client(int client_fd, xdp_umem* umem) {
    u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];

    while(1) {
        int ret = recv(client_fd, buffer, sizeof(buffer), 0);
        if(ret == 0) {
            std::cout << "client socket closed\n";
            break;
        }
        assert(ret > 0);

        u32 request = *((u32*)buffer);
        assert(request >= 0);
        assert(request < static_cast<u32>(RequestType::MAX));
        RequestType type = static_cast<RequestType>(request);
        switch(type) {
            case RequestType::ALLOC:
            {
                u64 frame = umem->alloc_frame();
                *(reinterpret_cast<u64*>(buffer)) = frame;
                ret = send(client_fd, buffer, sizeof(u64), MSG_EOR);
                assert(ret != -1);
            }
                break;
            case RequestType::DEALLOC:
            {
                u64 frame = *(reinterpret_cast<u64*>(buffer + sizeof(u32)));
                umem->free_frame(frame);
            }
                break;
            case RequestType::GET_PID_FD:
            {
                pid_t pid = getpid();
                *(reinterpret_cast<pid_t*>(buffer)) = pid;
                *(reinterpret_cast<int*>(buffer + sizeof(pid_t))) = umem->fd;
                // printf("pid: %d  fd: %d\n", pid, umem->fd);
                ret = send(client_fd, buffer, sizeof(pid_t) + sizeof(int), MSG_EOR);
                assert(ret != -1);
            }
                break;
            default:
                printf("unknown request type: %d\n", request);
                assert(0);
        }
    }

    close(client_fd);
}

static int create_server_socket() {
    sockaddr_un server_addr;
    int server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    assert(server_fd >= 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    unlink(DEFAULT_SOCKET_PATH);
    int ret = bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    assert(ret >= 0);

    ret = listen(server_fd, 20);
    assert(ret >= 0);

    return server_fd;
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
		printf("usage: manager {dummy_interface_name}\n");
		return 0;
	}
    int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);
    // printf("if_index: %d\n", if_index);

    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

    xdp_program program;
	program.load("./kern.o", if_index);

    xdp_umem* umem = new xdp_umem();
    umem->create();

    xdp_sock* sock = new xdp_sock();
    sock->fd = umem->fd;
    sock->create();
    sock->bind_to_device(if_index);

    // xdp_sock* sock2 = new xdp_sock();
    // sock2->fd = socket(AF_XDP, SOCK_RAW, 0);
    // assert(sock2->fd != -1);
    // sock2->create();
    // sock2->bind_to_device_shared(if_nametoindex("test"), umem->fd);


    std::thread thread_loop_enq_fr(loop_enq_fr, umem);
    std::thread thread_loop_deq_cr(loop_deq_cr, umem);

    int server_fd = create_server_socket();
    std::cout << "waiting for connections\n";
    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        assert(client_fd != -1);
        std::cout << "new client\n";
        new std::thread(serve_client, client_fd, umem);
    }

    close(server_fd);
    unlink(DEFAULT_SOCKET_PATH);

    return 0;
}