#!/bin/sh

clang src/*.cpp -lc++ -std=c++17 -Wall -Wextra -Wpedantic -I./lib/include -lfmt -o http-server
