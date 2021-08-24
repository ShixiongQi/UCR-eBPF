#!/bin/bash
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install update-manager-core

sudo apt-get dist-upgrade
sudo sed -i 's/lts/normal/g' /etc/update-manager/release-upgrades
sudo sed -i 's/focal/hirsute/g' /etc/apt/sources.list
sudo apt-get update
sudo apt-get upgrade
sudo apt-get dist-upgrade

