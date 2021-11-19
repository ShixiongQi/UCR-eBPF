#!/bin/bash

sudo ip link add s1 type veth peer name s2
sudo ip netns add test_ns
sudo ip link set s2 netns test_ns

sudo ip link set dev s1 up
sudo ip addr add dev s1 10.0.1.1/24

sudo ip netns exec test_ns ip link set dev s2 up
sudo ip netns exec test_ns ip addr add dev s2 10.0.1.2/24


# sudo ip link del test