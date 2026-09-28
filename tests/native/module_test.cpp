#include <cstdio>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: nexus_module_test <module> <contract.lua> <syntax.lua>\n");
        return 2;
    }

    // Fresh states and dynamic loads exercise lifetime without emulating nanos world.
    for (int iteration = 0; iteration < 20; ++iteration) {
#if defined(_WIN32)
        const auto library = LoadLibraryA(argv[1]);
        const auto entry = library ? reinterpret_cast<lua_CFunction>(
            GetProcAddress(library, "luaopen_nexus_bridge")) : nullptr;
#else
        const auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        const auto entry = library ? reinterpret_cast<lua_CFunction>(
            dlsym(library, "luaopen_nexus_bridge")) : nullptr;
#endif
        if (!entry) {
            std::fprintf(stderr, "Cannot load module entry point: %s\n", argv[1]);
            return 1;
        }
        auto* state = luaL_newstate();
        if (!state) {
            std::fprintf(stderr, "Cannot allocate Lua state\n");
            return 1;
        }
        luaL_openlibs(state);
        lua_pushcfunction(state, entry);
        int status = lua_pcall(state, 0, 1, 0);
        if (status == LUA_OK) {
            lua_setglobal(state, "nexus_bridge");
            status = luaL_dofile(state, argv[2]);
        }
        if (status == LUA_OK) {
            // Compile the host harness without pretending to supply the host APIs.
            status = luaL_loadfile(state, argv[3]);
        }
        if (status != LUA_OK) {
            std::fprintf(stderr, "Contract failed: %s\n", lua_tostring(state, -1));
        }
        lua_close(state);
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        if (status != LUA_OK) {
            return 1;
        }
    }
    std::puts("Module contract passed across 20 dynamic load/state lifetimes");
    return 0;
}
