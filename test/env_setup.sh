#!/bin/bash
sudo apt update
sudo apt upgrade
sudo apt install -y libcap-dev pkgconf libelf-dev clang llvm

pushd $(pwd)
cd ~
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make
sudo make install

cd ../include
sudo mkdir /usr/include/asm
sudo mkdir /usr/include/linux
sudo cp asm/*.h /usr/include/asm
sudo cp linux/*.h /usr/include/linux
popd


sudo ln /usr/lib64/libbpf.a /usr/lib/libbpf.a
sudo ln /usr/lib64/libbpf.so /usr/lib/libbpf.so
sudo ln /usr/lib64/libbpf.so.0 /usr/lib/libbpf.so.0
sudo ln /usr/lib64/libbpf.so.0.5.0 /usr/lib/libbpf.so.0.5.0


cd ~
git clone https://github.com/xdp-project/xdp-tutorial.git