#include "program.h"

namespace xdp
{
    void Program::load(const char* path, int if_index)
    {
        int ret;

        struct bpf_object* obj;

        struct bpf_prog_load_attr load_attr = {
            .file = path,
            .prog_type = BPF_PROG_TYPE_XDP
        };

        ret = bpf_prog_load_xattr(&load_attr, &obj, &prog_fd);
        assert(ret == 0);

        ret = bpf_set_link_xdp_fd(if_index, prog_fd, XDP_FLAGS_DRV_MODE);
        assert(ret >= 0);
        
        map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
        assert(map_fd >= 0);
    }

    void Program::update_map(int key, int value)
    {
        int ret = bpf_map_update_elem(map_fd, &key, &value, 0);
        assert(ret == 0);
    }
}