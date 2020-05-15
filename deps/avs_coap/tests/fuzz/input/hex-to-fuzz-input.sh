#!/usr/bin/env bash

sed -e 's/#.*$//g' | paste -sd\  | sed -e 's/[^0-9a-fA-F]//g' | xxd -r -p
