#!/bin/bash
sudo apt update
sudo apt upgrade
sudo apt install -y libcap-dev pkgconf libelf-dev clang llvm

pushd $(pwd)
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make
sudo make install

popd
pushd $(pwd)
git clone https://github.com/xdp-project/xdp-tutorial.git