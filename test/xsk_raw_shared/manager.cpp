#include "shared_definitions.hpp"
#include <bits/stdc++.h>

// struct for managing shared umem
struct xdp_umem_manager {
    struct xdp_umem umem;
    std::mutex mtx;
    // the index for next free frame position
    u32 next_idx;
    u64 free_frames[DEFAULT_NUM_FRAMES];
};

// struct for parameters to thread that handles a client request
struct thread_param {
    int client_fd;
    struct xdp_umem_manager* manager;
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

// return the number of free entries in umem fill or completion ring
static inline u32 umem_nb_free(struct xdp_umem_queue *q, u32 nb) {
	u32 free_entries = q->cached_cons - q->cached_prod;

	if(free_entries >= nb) {
		return free_entries;
	}

	q->cached_cons = *q->consumer + q->size;
	return q->cached_cons - q->cached_prod;
}

// return the number of usable entries in umem fill or completion ring
static inline u32 umem_nb_avail(struct xdp_umem_queue *q, u32 nb) {
	u32 entries = q->cached_prod - q->cached_cons;

	if(entries == 0) {
		q->cached_prod = *q->producer;
		entries = q->cached_prod - q->cached_cons;
	}

	return (entries > nb) ? nb : entries;
}

// fill descriptors into the fill ring
static inline size_t umem_fill_to_kernel(struct xdp_umem_queue *fq, u64 *d, size_t nb) {
	if(umem_nb_free(fq, nb) < nb) {
		return -1;
	}

	for(u32 i = 0; i<nb; i++) {
		u32 idx = fq->cached_prod++ &fq->mask;
		fq->ring[idx] = d[i];
	}

	u_smp_wmb();

	*fq->producer = fq->cached_prod;

	return nb;
}

// get descriptors from the completion ring
static inline size_t umem_complete_from_kernel(struct xdp_umem_queue *cq, u64 *d, size_t nb) {
	u32 entries = umem_nb_avail(cq, nb);

	u_smp_rmb();

	for(u32 i = 0; i < entries; i++) {
		u32 idx = cq->cached_cons++ & cq->mask;
		d[i] = cq->ring[idx];
	}

	if(entries > 0) {
		u_smp_wmb();
		*cq->consumer = cq->cached_cons;
	}

	return entries;
}

// create umem
static struct xdp_umem_manager* create_umem() {
    struct xdp_umem_manager *manager = (struct xdp_umem_manager*)calloc(1, sizeof(struct xdp_umem_manager));
    assert(manager != nullptr);

    struct xdp_umem *umem = &manager->umem;

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

    struct xdp_umem_reg reg;
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
    struct xdp_mmap_offsets off;
	socklen_t optlen = sizeof(off);
	ret = getsockopt(sfd, SOL_XDP, XDP_MMAP_OFFSETS, &off, &optlen);
	assert(ret == 0);
	
	// fill ring
	umem->fq.map = (u8*)mmap(0, off.fr.desc + DEFAULT_FILL_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED |MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_FILL_RING);
    assert(umem->fq.map != MAP_FAILED);

	umem->fq.mask = DEFAULT_FILL_RING_SIZE - 1;
	umem->fq.size = DEFAULT_FILL_RING_SIZE;
	umem->fq.producer = (u32*)(umem->fq.map + off.fr.producer);
	umem->fq.consumer = (u32*)(umem->fq.map + off.fr.consumer);
	umem->fq.ring = (u64*)(umem->fq.map + off.fr.desc);
	umem->fq.cached_cons = DEFAULT_FILL_RING_SIZE;

	// completion ring
	umem->cq.map = (u8*)mmap(0, off.cr.desc + DEFAULT_COMPLETION_RING_SIZE * sizeof(u64),
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_POPULATE,
						sfd, XDP_UMEM_PGOFF_COMPLETION_RING);
    assert(umem->cq.map != MAP_FAILED);

	umem->cq.mask = DEFAULT_COMPLETION_RING_SIZE - 1;
	umem->cq.size = DEFAULT_COMPLETION_RING_SIZE;
	umem->cq.producer = (u32*)(umem->cq.map + off.cr.producer);
	umem->cq.consumer = (u32*)(umem->cq.map + off.cr.consumer);
	umem->cq.ring = (u64*)(umem->cq.map + off.cr.desc);

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

static void produce_fill_ring(xdp_umem_manager* manager) {
    xdp_umem_queue *fq = &manager->umem.fq;
    while(1) {
        int free_entries = umem_nb_free(fq, 1);
        if(free_entries == 0) {
            continue;
        }
        assert(free_entries > 0);

        // free_entries > 0
        u64 frame = alloc_frame(manager);
        size_t ret = umem_fill_to_kernel(fq, &frame, 1);
        assert(ret == 1);
    }
}

static void consume_completion_ring(xdp_umem_manager* manager) {
    struct xdp_umem_queue *cq = &manager->umem.cq;

    while(1) {
        int avail_entries = umem_nb_avail(cq, 1);
        if(avail_entries == 0) {
            continue;
        }
        assert(avail_entries == 1);

        // avail_entries == 1
        u64 frame;
        int ret = umem_complete_from_kernel(cq, &frame, 1);
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
            printf("client socket closed\n");
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
    struct sockaddr_un server_addr;
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
    std::thread thread_produce_fill_ring(produce_fill_ring, manager);
    std::thread thread_consume_completion_ring(consume_completion_ring, manager);

    int server_fd = create_server_socket();
    printf("waiting for connections\n");
    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        assert(client_fd != -1);
        printf("new client\n");
        new std::thread(serve_client, client_fd, manager);
    }

    close(server_fd);
    unlink(DEFAULT_SOCKET_PATH);

    return 0;
}