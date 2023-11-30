#!/usr/bin/env bash

sudo apt-get install git build-essential cmake libmbedtls-dev zlib1g-dev
git submodule update --init
cmake .
make -j
