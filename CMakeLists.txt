cmake_minimum_required(VERSION 3.16)
project(PCController LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

add_executable(pccontroller
    src/main.cpp
)

target_link_libraries(pccontroller
    PRIVATE
        CURL::libcurl
)

if(WIN32)
    target_link_libraries(pccontroller PRIVATE windowscodecs ole32)
endif()
