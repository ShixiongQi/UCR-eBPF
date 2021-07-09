#include "shared_definitions.hpp"

// struct for managing shared umem
struct xdp_umem_manager {
    struct xdp_umem umem;
    std::mutex mtx;
    // the index for next free frame position
    u32 next_idx;
    u64 free_frames[DEFAULT_NUM_FRAMES];
};


static int should_exit = 0;

static void int_exit(int sig) {
	should_exit = 1;
}

static void* exit_checker(void* arguments) {
    while(!should_exit) {
        sleep(1);
    }
    exit(0);
    return 0;
}



// create umem
static struct xdp_umem_manager* create_umem() {
    xdp_umem_manager *manager = new xdp_umem_manager();
    xdp_umem *umem = &manager->umem;

    // initialize index
    manager->next_idx = DEFAULT_NUM_FRAMES + 1;
    for(int i=0; i < DEFAULT_NUM_FRAMES; i++) {
        manager->free_frames[i] = i * DEFAULT_FRAME_SIZE;
    }
    // since a socket descriptor is needed for creating umem, a dummy AF_XDP socket is created
    int sfd = socket(AF_XDP, SOCK_RAW, 0);
    assert(sfd != -1);
    manager->umem.fd = sfd;

    int fd = shm_open(DEFAULT_UMEM_FILE_NAME, O_RDWR | O_CREAT, 0777);
    assert(fd >= 0);

    int ret = ftruncate(fd, DEFAULT_UMEM_SIZE);
    assert(ret == 0);

    // no need to worry about aligning
    // on Linux, the mapping will be created at a nearby page boundary
    umem->frames = (u8*)mmap(NULL, DEFAULT_UMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(umem->frames != MAP_FAILED);

    xdp_umem_reg reg;
	reg.addr = (u64)umem->frames;
	reg.len = DEFAULT_UMEM_SIZE;
	reg.chunk_size = DEFAULT_FRAME_SIZE;
	reg.headroom = DEFAULT_HEADROOM;
	reg.flags = 0;

    ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_REG, &reg, sizeof(reg));
	assert(ret == 0);

    // set size for fill ring and completion ring
    int fq_size = DEFAULT_FILL_RING_SIZE;
	int cq_size = DEFAULT_COMPLETION_RING_SIZE;

    ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_FILL_RING, &fq_size, sizeof(int));
	assert(ret == 0);

	ret = setsockopt(sfd, SOL_XDP, XDP_UMEM_COMPLETION_RING, &cq_size, sizeof(int));
	assert(ret == 0);

    // get fill ring and completion ring
    xdp_mmap_offsets off;
	socklen_t optlen = sizeof(off);
	ret = getsockopt(sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	assert(ret == 0);
	
	// fill ring
	umem->fr.map = (u8*)mmap(0, off.fr.desc + DEFAULT_FILL_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED |MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_FILL_RING);
    assert(umem->fr.map != MAP_FAILED);

	umem->fr.mask = DEFAULT_FILL_RING_SIZE - 1;
	umem->fr.size = DEFAULT_FILL_RING_SIZE;
	umem->fr.producer = (u32*)(umem->fr.map + off.fr.producer);
	umem->fr.consumer = (u32*)(umem->fr.map + off.fr.consumer);
	umem->fr.ring = (u64*)(umem->fr.map + off.fr.desc);
	umem->fr.cached_cons = DEFAULT_FILL_RING_SIZE;

	// completion ring
	umem->cr.map = (u8*)mmap(0, off.cr.desc + DEFAULT_COMPLETION_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_COMPLETION_RING);
    assert(umem->cr.map != MAP_FAILED);

	umem->cr.mask = DEFAULT_COMPLETION_RING_SIZE - 1;
	umem->cr.size = DEFAULT_COMPLETION_RING_SIZE;
	umem->cr.producer = (u32*)(umem->cr.map + off.cr.producer);
	umem->cr.consumer = (u32*)(umem->cr.map + off.cr.consumer);
	umem->cr.ring = (u64*)(umem->cr.map + off.cr.desc);

	umem->fd = sfd;

	return manager;
}

// allocate a umem frame, return the descriptor
static u64 alloc_frame(struct xdp_umem_manager* manager) {
    manager->mtx.lock();
    assert(manager->next_idx != 0);

	u64 frame = manager->free_frames[--manager->next_idx];
	//printf("allocate %p\n", (void*)frame);
    manager->mtx.unlock();
	return frame;
}


// free a umem frame.
static void free_frame(struct xdp_umem_manager* manager, u64 frame) {
    manager->mtx.lock();
	// based on my experiment, it seems that rx ring does not always return aligned address
	frame = frame & (~(DEFAULT_FRAME_SIZE-1));
    assert(manager->next_idx < DEFAULT_NUM_FRAMES + 1);

	manager->free_frames[manager->next_idx++] = frame;
	//printf("free %p\n", (void*)frame);
    manager->mtx.unlock();
}

// continually enqueue fill ring in a loop
static void loop_enq_fr(xdp_umem_manager* manager) {
    xdp_umem_queue *fr = &manager->umem.fr;
    while(1) {
        int free_entries = fr->fr_nb_free(1);
        if(free_entries == 0) {
            continue;
        }
        assert(free_entries > 0);

        // free_entries > 0
        u64 frame = alloc_frame(manager);
        size_t ret = fr->fr_enq(&frame, 1);
        assert(ret == 1);
    }
}

// continually dequeue completion ring in a loop and free the descriptors
static void loop_deq_cr(xdp_umem_manager* manager) {
    struct xdp_umem_queue *cr = &manager->umem.cr;

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

        free_frame(manager, frame);
    }
}

// the thread function that will serve a client's api call
static void serve_client(int client_fd, xdp_umem_manager* manager) {
    u8 buffer[DEFAULT_SOCKET_MAX_BUFFER_SIZE];

    while(1) {
        int ret = recv(client_fd, buffer, sizeof(buffer), MSG_WAITALL);
        if(ret == 0) {
            std::cout << "client socket closed\n";
            break;
        }
        assert(ret > 0);

        u32 request = *((u32*)buffer);
        switch(request) {
            case REQUEST_ALLOC:
            {
                u64 frame = alloc_frame(manager);
                *((u64*)buffer) = frame;
                ret = send(client_fd, buffer, sizeof(u64), MSG_EOR);
                assert(ret != -1);
            }
                break;
            case REQUEST_DEALLOC:
            {
                u64 frame = *((u64*)(buffer + sizeof(u32)));
                free_frame(manager, frame);
            }
                break;
            default:
                printf("unknown request type: %d\n", request);
                assert(0);
        }
    }

    close(client_fd);
}

int create_server_socket() {
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
    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    pthread_t th;
    int ret = pthread_create(&th, NULL, exit_checker, NULL);
    assert(ret == 0);

    struct xdp_umem_manager* manager = create_umem();
    std::thread thread_loop_enq_fr(loop_enq_fr, manager);
    std::thread thread_loop_deq_cr(loop_deq_cr, manager);

    int server_fd = create_server_socket();
    std::cout << "waiting for connections\n";
    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        assert(client_fd != -1);
        std::cout << "new client\n";
        new std::thread(serve_client, client_fd, manager);
    }

    close(server_fd);
    unlink(DEFAULT_SOCKET_PATH);

    return 0;
}