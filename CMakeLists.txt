cmake_minimum_required(VERSION 3.22)
project(sol)

set(CMAKE_CXX_STANDARD 17)

add_definitions("-DDEBUG")

add_executable(sol malloc_3.cpp altmain.cpp altmain.h)
