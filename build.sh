#!/bin/bash

set -e
pkg-config --print-errors --exists 'fmt >= 10'

g++ src/*.cpp -std=c++20 -Wall -Wextra -Wpedantic $(pkg-config --cflags --libs fmt) -I./lib/include -o http-server
