cmake_minimum_required(VERSION 4.3)
project(FishOS)

set(CMAKE_CXX_STANDARD 20)

add_executable(FishOS main.cpp
        kernel.cpp)
