cmake_minimum_required(VERSION 3.21)
project(NexusBridge VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This Bridge checkpoint requires a 64-bit toolchain.")
endif()
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Windows and Linux builds are currently targeted.")
endif()

include(CTest)
add_subdirectory(bridge)
