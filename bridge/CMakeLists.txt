set(NEXUS_SDK_REVISION "8bea6bc806507fe82ab08f742f9c209fb4bae8f7")
set(sdk_dir "${CMAKE_CURRENT_SOURCE_DIR}/deps/module-sdk")
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${sdk_dir}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_revision OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE sdk_result ERROR_QUIET)
if(NOT sdk_result EQUAL 0 OR NOT sdk_revision STREQUAL NEXUS_SDK_REVISION)
    message(FATAL_ERROR "Initialize the pinned SDK: git submodule update --init --recursive")
endif()

if(WIN32)
    set(sdk_library "${sdk_dir}/lib/lua.lib")
    set(sdk_sha256 "0b4f64aee64b602c7c20a92631187ec4bb4bab8c6c156e492c2629799bb6628b")
else()
    set(sdk_library "${sdk_dir}/lib/liblua.a")
    set(sdk_sha256 "3a4b173a34ccd34a0fe8f3a9dbb8647473527eae17dcc9acef04f1b1dcb6b67b")
endif()
if(NOT EXISTS "${sdk_library}")
    message(FATAL_ERROR "SDK library missing. Run git -C bridge/deps/module-sdk lfs pull")
endif()
file(SHA256 "${sdk_library}" sdk_actual_sha256)
if(NOT sdk_actual_sha256 STREQUAL sdk_sha256)
    message(FATAL_ERROR "SDK library hash mismatch (possibly an LFS pointer). Run git -C bridge/deps/module-sdk lfs pull")
endif()

add_library(nexus_lua_sdk STATIC IMPORTED)
set_target_properties(nexus_lua_sdk PROPERTIES
    IMPORTED_LOCATION "${sdk_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${sdk_dir}/include")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET nexus_lua_sdk PROPERTY INTERFACE_LINK_LIBRARIES "m;${CMAKE_DL_LIBS}")
endif()

configure_file(version.h.in generated/nexus_version.h @ONLY)
add_library(nexus_bridge SHARED src/module.cpp)
target_compile_features(nexus_bridge PRIVATE cxx_std_17)
target_include_directories(nexus_bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(nexus_bridge PRIVATE nexus_lua_sdk)
set_target_properties(nexus_bridge PROPERTIES CXX_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_options(nexus_bridge PRIVATE /W4 /WX)
else()
    target_compile_options(nexus_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(nexus_bridge PRIVATE "-Wl,--exclude-libs,ALL")
endif()

install(TARGETS nexus_bridge
    RUNTIME DESTINATION Packages/nexus-bridge
    LIBRARY DESTINATION Packages/nexus-bridge)
install(FILES "${PROJECT_SOURCE_DIR}/packages/nexus-bridge/Package.toml"
    DESTINATION Packages/nexus-bridge)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${sdk_dir}/LICENSE.txt"
    DESTINATION Packages/nexus-bridge/licenses)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-bridge-check"
    DESTINATION Packages)

if(BUILD_TESTING)
    add_executable(nexus_module_test "${PROJECT_SOURCE_DIR}/tests/native/module_test.cpp")
    target_compile_features(nexus_module_test PRIVATE cxx_std_17)
    target_link_libraries(nexus_module_test PRIVATE nexus_lua_sdk ${CMAKE_DL_LIBS})
    add_test(NAME bridge_module_contract COMMAND nexus_module_test
        "$<TARGET_FILE:nexus_bridge>" "${PROJECT_SOURCE_DIR}/tests/native/module_contract.lua"
        "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-bridge-check/Server/Index.lua")
endif()
