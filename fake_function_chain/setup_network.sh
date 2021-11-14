!/bin/bash

sudo ip link add test type veth peer name test1
sudo ip netns add test_ns
sudo ip link set test1 netns test_ns

sudo ip link set dev test up
sudo ip addr add dev test 10.0.0.1/24

# sudo ip netns exec test_ns bash
# ip link set dev test1 up
# ip addr add dev test1 10.0.0.2/24


# sudo ip link del test