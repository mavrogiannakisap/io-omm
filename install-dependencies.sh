#!/bin/bash

# Install dependencies
sudo apt update && sudo apt upgrade
sudo apt install -y python3-pip

sudo apt install ninja-build gettext libtool libtool-bin autoconf automake cmake g++ pkg-config unzip
sudo pip3 install conan==1.64.0

