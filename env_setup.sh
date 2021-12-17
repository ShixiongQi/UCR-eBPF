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

sudo cp /usr/lib64/libbpf.so.0.6.0 /lib/x86_64-linux-gnu/libbpf.so.0.6.0
cd /lib/x86_64-linux-gnu
sudo ln -s -f libbpf.so.0.6.0 libbpf.so.0

popd