#!/bin/bash


clang -O2 -emit-llvm -c bounce_kern.c -o - | llc -march=bpf -filetype=obj -o bounce_kern.o
clang -O2 -emit-llvm -c rx_kern.c -o - | llc -march=bpf -filetype=obj -o rx_kern.o

clang++ -std=gnu++20 -lbpf -lpthread -lrt manager.cpp -o manager
clang++ -std=gnu++20 -lbpf -lpthread -lrt client.cpp -o client
clang++ -std=gnu++20 -lbpf -lpthread -lrt bounce.cpp -o bounce

# /sys/kernel/debug/tracing/trace_pipe
