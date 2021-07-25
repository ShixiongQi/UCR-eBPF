#!/bin/bash

clang++ -std=gnu++20 -lpthread -lrt client.cpp -o client
clang++ -std=gnu++20 -lpthread -lrt server.cpp -o server

