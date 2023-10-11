cmake_minimum_required(VERSION 3.22)

project(HttpServer VERSION 0.1)

find_package(Boost 1.74 REQUIRED)

find_package(fmt 10.0.0 QUIET)
if(NOT fmt_FOUND)
    include(FetchContent)
    set(FETCHCONTENT_QUIET False)
    FetchContent_Declare(
        fmt
        URL https://github.com/fmtlib/fmt/releases/download/10.1.1/fmt-10.1.1.zip
        URL_HASH SHA512=ea5fec1330e14a7e73f5820afbcb0131320ae00e567e9f66479592dfc5eae7f275fa42b2be3d6afe034e14460365cb5be8bfd8c975d3b71143308e3cbf788e27
        )
    FetchContent_MakeAvailable(fmt)
endif()

add_library(http_server_compiler_flags INTERFACE)
target_compile_features(http_server_compiler_flags INTERFACE cxx_std_20)
# fmt library causes warnings with -fsanitze=vptr, so disable that check
target_compile_options(http_server_compiler_flags INTERFACE "-Wall;-Wextra;-Wpedantic;-fsanitize=address,undefined;-fno-sanitize=vptr")
target_link_options(http_server_compiler_flags INTERFACE "-fsanitize=address,undefined")

add_executable(http-server)
add_subdirectory(src)
target_include_directories(http-server PRIVATE "${PROJECT_SOURCE_DIR}/lib/include" ${Boost_INCLUDE_DIRS})
target_link_libraries(http-server PRIVATE http_server_compiler_flags fmt::fmt)
