cmake_minimum_required(VERSION 3.3)
project(CMPUT379_ASSIGN3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES procnanny.server.c memwatch.h procnanny.server.h procnanny.client.c procnanny.client.h)
add_executable(CMPUT379_ASSIGN3 ${SOURCE_FILES})