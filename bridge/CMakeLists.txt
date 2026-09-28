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
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/lua-source.cmake")
endif()

configure_file(version.h.in generated/nexus_version.h @ONLY)
find_package(Threads REQUIRED)
add_library(nexus_wire STATIC src/frame.cpp src/frame_queue.cpp)
target_compile_features(nexus_wire PUBLIC cxx_std_17)
target_include_directories(nexus_wire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(nexus_wire PUBLIC Threads::Threads)
set_target_properties(nexus_wire PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
if(MSVC)
    target_compile_options(nexus_wire PRIVATE /W4 /WX)
else()
    target_compile_options(nexus_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(nexus_transport STATIC src/transport.cpp src/listener.cpp)
target_link_libraries(nexus_transport PUBLIC nexus_wire)
set_target_properties(nexus_transport PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
if(WIN32)
    target_link_libraries(nexus_transport PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(nexus_transport PRIVATE /W4 /WX)
else()
    target_compile_options(nexus_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(nexus_bridge SHARED src/module.cpp src/transport_lua.cpp)
target_compile_features(nexus_bridge PRIVATE cxx_std_17)
target_include_directories(nexus_bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(nexus_bridge PRIVATE nexus_lua_sdk nexus_transport)
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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES "${nexus_lua_SOURCE_DIR}/doc/readme.html"
        DESTINATION Packages/nexus-bridge/licenses RENAME lua-5.4.9-readme.html)
endif()
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-bridge-check"
    DESTINATION Packages)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-transport-check"
    DESTINATION Packages)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-listener-check"
    DESTINATION Packages)

if(BUILD_TESTING)
    add_executable(nexus_transport_test "${PROJECT_SOURCE_DIR}/tests/native/transport_test.cpp")
    target_link_libraries(nexus_transport_test PRIVATE nexus_transport)
    add_test(NAME bridge_transport_contract COMMAND nexus_transport_test)
    set_tests_properties(bridge_transport_contract PROPERTIES TIMEOUT 90)
    add_executable(nexus_wire_test "${PROJECT_SOURCE_DIR}/tests/native/wire_test.cpp")
    target_link_libraries(nexus_wire_test PRIVATE nexus_wire)
    add_test(NAME bridge_wire_contract COMMAND nexus_wire_test)
    set_tests_properties(bridge_wire_contract PROPERTIES TIMEOUT 30)
    add_executable(nexus_module_test "${PROJECT_SOURCE_DIR}/tests/native/module_test.cpp")
    target_compile_features(nexus_module_test PRIVATE cxx_std_17)
    target_link_libraries(nexus_module_test PRIVATE nexus_lua_sdk ${CMAKE_DL_LIBS})
    add_test(NAME bridge_module_contract COMMAND nexus_module_test
        "$<TARGET_FILE:nexus_bridge>" "${PROJECT_SOURCE_DIR}/tests/native/module_contract.lua"
        "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-bridge-check/Server/Index.lua")
    add_test(NAME bridge_transport_lua_contract COMMAND nexus_module_test
        "$<TARGET_FILE:nexus_bridge>" "${PROJECT_SOURCE_DIR}/tests/native/transport_contract.lua"
        "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-transport-check/Server/Index.lua"
        "${PROJECT_SOURCE_DIR}/tests/runtime/nexus-listener-check/Server/Index.lua")
    set_tests_properties(bridge_transport_lua_contract PROPERTIES TIMEOUT 60)
endif()
