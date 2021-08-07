#!/bin/bash
ip netns add test
ip link add dev test type veth peer name veth0 netns test

ip link set dev test up
ip addr add dev test 10.0.0.1/24

ip -n test link set dev lo up
ip -n test link set dev veth0 up
ip -n test addr add dev veth0 10.0.0.2/24


# teardown

# ip link del dev test
# ip netns del test

# enter

# ip netns exec test /bin/bash