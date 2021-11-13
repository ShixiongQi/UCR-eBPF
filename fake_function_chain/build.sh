#!/bin/bash

clang -O2 -emit-llvm -c bounce_kern.c -o - | llc -march=bpf -filetype=obj -o bounce_kern.o

clang -O2 -emit-llvm -c rx_kern.c -o - | llc -march=bpf -filetype=obj -o rx_kern.o

clang++ -std=gnu++20 -lbpf -lpthread -lrt -o gateway gateway.cpp communicator.cpp context.cpp util.cpp
clang++ -std=gnu++20 -lbpf -lpthread -lrt -o function function.cpp communicator.cpp context.cpp util.cpp
clang++ -std=gnu++20 -lbpf -lpthread -lrt -o bounce bounce.cpp