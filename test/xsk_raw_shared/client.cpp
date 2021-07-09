#include "shared_definitions.hpp"

// struct containing information for ebpf kernel program
struct xdp_program {
	struct bpf_object* obj;
	int prog_fd;
	std::unordered_map<std::string, int> bpf_map;

	void load(const char* path, int if_index) {
		int ret;
		struct bpf_prog_load_attr load_attr = {
			.file = path,
			.prog_type = BPF_PROG_TYPE_XDP
		};

		ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
		assert(ret == 0);

		ret = bpf_set_link_xdp_fd(if_index, prog_fd, XDP_FLAGS_DRV_MODE);
		assert(ret >= 0);
	}

	void update_map(const char* map_name, int key, int value) {
		if(bpf_map.find(map_name) == bpf_map.end()) {
			struct bpf_map *map = bpf_object__find_map_by_name(obj, map_name);
			int map_fd = bpf_map__fd(map);
			assert(map_fd >= 0);
			bpf_map[map_name] = map_fd;
		}

		int ret = bpf_map_update_elem(bpf_map[map_name], &key, &value, 0);
		assert(ret == 0);
	}
};

// create a client socket for ipc use
int create_ipc_socket() {
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	assert(fd >= 0);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, DEFAULT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

	int ret = connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
	assert(ret != -1);

	return fd;
}

int main(int argc, char* argv[]) {
	if(argc != 2) {
		printf("usage: client {interface_name}\n");
		return 0;
	}
	int if_index = if_nametoindex(argv[1]);
	assert(if_index != 0);


    signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

    allow_unlimited_locking();

	int client_fd = create_ipc_socket();
	
	xdp_umem_shared* umem = new xdp_umem_shared();
	umem->map_shared_memory();
	umem->get_shared_fd(client_fd);

	xdp_sock* sock = new xdp_sock();
	sock->umem = umem;
	sock->create();
	sock->bind_to_device(if_index);


}