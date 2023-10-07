# http-server
A simple HTTP 1.1 server written with C++ using Boost Asio with coroutines.

Features:
- Asynchronous IO with Boost Asio (File IO is blocking, since Asio doesn't support it with coroutines)
- Serves files from the `public` folder
- In addition, serves a static index HTML page
- For parsing the requests a ring buffer is used. It uses virtual memory to map the buffer multiple times to memory, which makes it easier to use
- LRU cache for the files

Tested on macOS 12.6.3 with Apple Clang 13.1.6 and Ubuntu 22.04.3 with GNU C++ Compiler 11.4.0.


## Compiling
First install fmt library version 10.1.1, boost 1.74.0 and other needed packages. This can be done with the one of the following ways depending on the system:
```
# macOS with Homebrew
brew install fmt@10.1.1 boost@1.74.0

# Ubuntu
sudo apt install build-essential cmake pkg-config libboost1.74-dev
cd ~
wget https://github.com/fmtlib/fmt/releases/download/10.1.1/fmt-10.1.1.zip
unzip fmt-10.1.1.zip
mkdir -p fmt-10.1.1/build && cd "$_"
cmake -DFMT_TEST:BOOLEAN=OFF ..
make
sudo make install
```

To compile, run the build script in this repository with
```
./build.sh
```


## Usage
After compiling the project, start the server with
```
./http-server
```
By default, the server will run on port 3000, but the port can be changed by giving it as an argument:
```
./http-server 4000
```

The server will serve a static HTML response on paths `/` and `/index.html` and files from the `public` directory.
You can make a request, for example, by typing
```
telnet localhost 3000
GET / HTTP/1.1

```


