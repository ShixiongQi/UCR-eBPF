#!/bin/bash
sudo apt update
sudo apt upgrade
sudo apt install -y libcap-dev pkgconf libelf-dev clang llvm

pushd $(pwd)
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make
sudo make install
echo "/usr/lib64/" | sudo tee -a /etc/ld.so.conf
sudo ldconfig
