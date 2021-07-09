#!/bin/bash


clang -O2 -emit-llvm -c kern.c -o - | llc -march=bpf -filetype=obj -o kern.o

clang++ -lpthread -lrt manager.cpp -o manager