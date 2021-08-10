#!/bin/bash
ip link add dev test type veth peer name test1

ip link set dev test up
ip addr add dev test 10.0.0.1/24

ip link set dev test1 up
ip addr add dev test1 10.0.0.2/24

cp -a /xdp_init/. /xdp

# teardown

# ip link del dev test
