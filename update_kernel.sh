#!/bin/bash
mkdir kernel_update
cd kernel_update

wget http://archive.ubuntu.com/ubuntu/pool/universe/d/dwarves-dfsg/dwarves_1.17-1_amd64.deb
sudo dpkg -i dwarves_1.17-1_amd64.deb

wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.48.tar.xz
unxz -v linux-5.10.48.tar.xz
tar xf linux-5.10.48.tar
cd linux-5.10.48/

cp -v /boot/config-$(uname -r) .config
sudo apt update
sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev
make menuconfig
scripts/config --disable SYSTEM_TRUSTED_KEYS
make -j $(nproc)

sudo make modules_install
sudo make headers_install INSTALL_HDR_PATH=/usr
sudo make install

sudo update-initramfs -c -k 5.10.48
sudo update-grub

echo "please reboot the machine"