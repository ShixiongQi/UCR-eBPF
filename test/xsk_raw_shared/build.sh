#!/bin/bash


clang -O2 -emit-llvm -c kern.c -o - | llc -march=bpf -filetype=obj -o kern.o

clang++ -std=gnu++17 -lpthread -lrt manager.cpp -o manager