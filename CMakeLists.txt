cmake_minimum_required(VERSION 3.24)
project(NexusBridge VERSION 0.3.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This Bridge checkpoint requires a 64-bit toolchain.")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$"
        OR (MSVC AND NOT CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64"))
    message(FATAL_ERROR "The documented nanos world server target is x86_64. ARM hosts using x86_64 emulation need the x86_64 module, not a native ARM module.")
endif()
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Windows and Linux builds are currently targeted.")
endif()

include(CTest)
add_subdirectory(bridge)
