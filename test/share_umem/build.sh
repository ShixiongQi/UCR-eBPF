#!/bin/bash

# C_INCLUDE_PATH=/usr/src/linux-headers-5.12.13-051213/include
# export C_INCLUDE_PATH
# echo | gcc -xc -E -v -

clang -O2 -emit-llvm -c kern.c -o - | llc -march=bpf -filetype=obj -o kern.o
clang -lbpf xsk_test.c -o xsk_test