#!/bin/bash
sudo ip link add dev test type veth peer name test1

sudo ip link set dev test up
sudo ip addr add dev test 10.0.0.1/24

sudo ip link set dev test1 up
sudo ip addr add dev test1 10.0.0.2/24