#!/usr/bin/env bash

git submodule update --init
cmake .
make -j
