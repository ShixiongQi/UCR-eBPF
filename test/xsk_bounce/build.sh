#!/bin/bash


clang -O2 -emit-llvm -c bounce_kern.c -o - | llc -march=bpf -filetype=obj -o bounce_kern.o
clang++ -std=gnu++20 -lbpf -lpthread -lrt bounce.cpp -o bounce



clang -O2 -emit-llvm -c rx_kern.c -o - | llc -march=bpf -filetype=obj -o rx_kern.o

clang++ -std=gnu++20 -lbpf -lpthread -lrt -o manager manager.cpp communication.cpp nanotime.cpp program.cpp sock.cpp umem.cpp util.cpp
clang++ -std=gnu++20 -lbpf -lpthread -lrt -o client client.cpp  communication.cpp nanotime.cpp program.cpp sock.cpp umem.cpp util.cpp
clang++ -std=gnu++20 -shared -fPIC -lbpf -lpthread -lrt -o libbpfclient.so client_lib.cpp  communication.cpp nanotime.cpp program.cpp sock.cpp umem.cpp util.cpp

cp bounce_kern.o bounce rx_kern.o manager client ../docker/
cp libbpfclient.so ../knative/libbpfclient.so
# /sys/kernel/debug/tracing/trace_pipe
