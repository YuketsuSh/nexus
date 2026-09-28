extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "nexus_version.h"

namespace {
int info(lua_State* state) {
    if (lua_gettop(state) != 0) {
        return luaL_error(state, "nexus_bridge.info expects no arguments");
    }

    lua_createtable(state, 0, 6);
    lua_pushinteger(state, NEXUS_BRIDGE_ABI);
    lua_setfield(state, -2, "abi_version");
    lua_pushliteral(state, NEXUS_BRIDGE_VERSION);
    lua_setfield(state, -2, "bridge_version");
    lua_pushliteral(state, LUA_RELEASE);
    lua_setfield(state, -2, "lua_headers");
    // This is the linked SDK's version, not a claim about the host executable.
    lua_pushnumber(state, lua_version(state));
    lua_setfield(state, -2, "linked_lua_api_version");
    lua_pushliteral(state, NEXUS_SDK_REVISION);
    lua_setfield(state, -2, "sdk_revision");
    lua_pushinteger(state, sizeof(void*) * 8);
    lua_setfield(state, -2, "pointer_bits");
    return 1;
}
}

#if defined(_WIN32)
#define NEXUS_EXPORT __declspec(dllexport)
#else
#define NEXUS_EXPORT __attribute__((visibility("default")))
#endif

extern "C" NEXUS_EXPORT int luaopen_nexus_bridge(lua_State* state) {
    const luaL_Reg functions[] = {{"info", info}, {nullptr, nullptr}};
    luaL_newlib(state, functions);
    return 1;
}
