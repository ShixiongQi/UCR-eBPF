#!/bin/bash
cd ~
wget https://raw.githubusercontent.com/pimlie/ubuntu-mainline-kernel.sh/master/ubuntu-mainline-kernel.sh
chmod +x ubuntu-mainline-kernel.sh
sudo ./ubuntu-mainline-kernel.sh -i
sudo reboot