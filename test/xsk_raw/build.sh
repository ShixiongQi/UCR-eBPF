#!/bin/bash


clang -O2 -emit-llvm -c kern.c -o - | llc -march=bpf -filetype=obj -o kern.o

clang -lbpf -lrt xsk.c -o xsk