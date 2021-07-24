#!/bin/bash


clang -O2 -emit-llvm -c kern.c -o - | llc -march=bpf -filetype=obj -o kern.o

clang++ -std=gnu++20 -lbpf -lpthread -lrt manager.cpp -o manager
clang++ -std=gnu++20 -lbpf -lpthread -lrt client.cpp -o client

# /sys/kernel/debug/tracing/trace_pipe
