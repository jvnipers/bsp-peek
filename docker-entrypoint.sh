#!/bin/bash

git config --global --add safe.directory /app
cd build
python3 ../configure.py --enable-optimize
ambuild
