#!/bin/bash

gcc -fPIC -shared -o libshare.so share.c
clang -ldl main.c -o main

clang -lrt main2.c -o main2