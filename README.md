# http-server
A simple HTTP 1.1 server written with C++ using Boost Asio with coroutines.

Features:
- Asynchronous IO with Boost Asio (File IO is blocking, since Asio doesn't support it with coroutines or at all on macOS)
- Serves files from a folder
- In addition, serves a static index HTML page
- For parsing the requests a ring buffer is used. It uses virtual memory to map the buffer multiple times to memory, which makes it easier to use
- LRU cache for the files
- Uses CMake as the build system

Tested on macOS 12.6.3 with Apple Clang 13.1.6 and Ubuntu 22.04.3 with GNU C++ Compiler 11.4.0.


## Compiling
First install CMake 3.22 or newer and Boost 1.74 or newer. Optionally, install fmt library version 10 (it will be built from source, if not found). This can be done with the one of the following ways depending on the system:
```
# macOS with Homebrew
brew install cmake boost fmt
# Ubuntu
sudo apt install build-essential cmake libboost-dev
```

To compile, run
```
mkdir build && cd $_
cmake ..
cmake --build . -j 4
```
This will result in a binary named `http-server` inside the build folder.

## Usage
After compiling the project, start the server with
```
./http-server
```
By default, the server will run on port 3000, but the port can be changed by setting environment variable `PORT`:
```
PORT=4000 ./http-server
```

The server will serve a static HTML response on paths `/` and `/index.html` and, by default, files from `public` directory in the current working directory. The path to the desired public directory can be given as an argument to `http-server`:
```
./http-server ../public
```

A request to the server can be made by typing
```
telnet localhost 3000
GET / HTTP/1.1

```


