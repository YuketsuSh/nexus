# The SDK's Linux archive is non-PIC. Rebuild its exact upstream Lua version,
# checking every supplied SDK header before linking it into the shared module.
include(FetchContent)
FetchContent_Declare(nexus_lua
    URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nexus_lua)

file(GLOB sdk_headers "${sdk_dir}/include/*.h")
foreach(header IN LISTS sdk_headers)
    get_filename_component(header_name "${header}" NAME)
    file(READ "${header}" sdk_header)
    file(READ "${nexus_lua_SOURCE_DIR}/src/${header_name}" source_header)
    string(REPLACE "\r\n" "\n" sdk_header "${sdk_header}")
    string(REPLACE "\r\n" "\n" source_header "${source_header}")
    if(NOT sdk_header STREQUAL source_header)
        message(FATAL_ERROR "Lua source differs from pinned SDK header: ${header_name}")
    endif()
endforeach()

set(lua_units lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo
    ldump lfunc lgc linit liolib llex lmathlib lmem loadlib lobject lopcodes
    loslib lparser lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib
    lvm lzio)
set(lua_sources)
foreach(unit IN LISTS lua_units)
    list(APPEND lua_sources "${nexus_lua_SOURCE_DIR}/src/${unit}.c")
endforeach()
add_library(nexus_lua_sdk STATIC ${lua_sources})
set_target_properties(nexus_lua_sdk PROPERTIES
    POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden C_STANDARD 99)
target_include_directories(nexus_lua_sdk PUBLIC "${sdk_dir}/include")
target_compile_definitions(nexus_lua_sdk PRIVATE LUA_USE_LINUX)
target_link_libraries(nexus_lua_sdk PUBLIC m ${CMAKE_DL_LIBS})
