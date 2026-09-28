#include "transport_lua.h"
#include "transport.h"
#include <new>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {
constexpr const char* handle_type = "nexus.transport.v1";
constexpr const char* quota_key = "nexus.transport.quota.v1";
struct Quota { unsigned active; };
struct Handle {
    nexus::net::Transport* transport;
    nexus::wire::Frame* pending;
    Quota* quota;
};

Handle* checked(lua_State* state) {
    return static_cast<Handle*>(luaL_checkudata(state, 1, handle_type));
}
void release(Handle* handle) {
    delete handle->pending;
    handle->pending = nullptr;
    if (handle->transport) {
        delete handle->transport; // Join completes before userdata/native library disposal.
        handle->transport = nullptr;
        --handle->quota->active;
    }
}
int close_handle(lua_State* state) {
    release(checked(state));
    return 0;
}
int snapshot(lua_State* state) {
    auto* handle = checked(state);
    const auto value = handle->transport ? handle->transport->snapshot()
        : nexus::net::Snapshot{nexus::net::State::closed, nexus::net::Reason::local_close, 0};
    lua_pushstring(state, nexus::net::name(value.state));
    lua_pushstring(state, nexus::net::name(value.reason));
    lua_pushinteger(state, value.port);
    return 3;
}
int send(lua_State* state) {
    auto* handle = checked(state);
    luaL_checktype(state, 2, LUA_TSTRING);
    std::size_t size = 0;
    const char* bytes = lua_tolstring(state, 2, &size);
    auto result = nexus::wire::QueueStatus::closed;
    bool failed = false;
    try {
        if (handle->transport) result = handle->transport->send(std::string_view(bytes, size));
    } catch (...) { failed = true; }
    // Lua may longjmp on allocation failure. No C++ RAII locals span these calls.
    lua_pushboolean(state, !failed && result == nexus::wire::QueueStatus::ok);
    lua_pushstring(state, failed ? "resource_error" : nexus::net::name(result));
    return 2;
}
int receive(lua_State* state) {
    auto* handle = checked(state);
    auto status = nexus::wire::QueueStatus::closed;
    bool failed = false;
    try {
        if (handle->pending) status = nexus::wire::QueueStatus::ok;
        else if (handle->transport) {
            auto result = handle->transport->receive();
            status = result.status;
            if (result.frame) handle->pending = new nexus::wire::Frame(std::move(*result.frame));
        }
    } catch (...) { failed = true; }
    if (failed) {
        release(handle);
        lua_pushnil(state);
        lua_pushliteral(state, "resource_error");
        return 2;
    }
    if (handle->pending) {
        // If Lua allocation fails, the retained frame remains owned by userdata
        // and is released by __gc (or retried on the next receive call).
        lua_pushlstring(state, handle->pending->payload.data(), handle->pending->payload.size());
    } else lua_pushnil(state);
    lua_pushstring(state, nexus::net::name(status));
    delete handle->pending;
    handle->pending = nullptr;
    return 2;
}

int create(lua_State* state, bool listen) {
    luaL_checktype(state, 1, LUA_TSTRING);
    std::size_t size = 0;
    const char* address = lua_tolstring(state, 1, &size);
    const auto port = luaL_checkinteger(state, 2);
    if (lua_gettop(state) != 2 || size == 0 || size > 15 || port < (listen ? 0 : 1) || port > 65535)
        return luaL_error(state, "expected numeric IPv4 address and port (listen: 0..65535; connect: 1..65535)");
    lua_getfield(state, LUA_REGISTRYINDEX, quota_key);
    auto* quota = static_cast<Quota*>(lua_touserdata(state, -1));
    if (quota->active >= 16) {
        lua_pushnil(state);
        lua_pushliteral(state, "handle_limit");
        return 2;
    }
    auto* handle = new (lua_newuserdatauv(state, sizeof(Handle), 1)) Handle{nullptr, nullptr, quota};
    luaL_setmetatable(state, handle_type);
    lua_pushvalue(state, -2); // Keep quota alive at least as long as this handle.
    lua_setiuservalue(state, -2, 1);
    try { handle->transport = new nexus::net::Transport(listen, std::string(address, size), static_cast<std::uint16_t>(port)); }
    catch (...) { /* No partially created worker escapes its constructor. */ }
    if (!handle->transport) {
        lua_pushnil(state);
        lua_pushliteral(state, "resource_error");
        return 2;
    }
    ++quota->active;
    return 1;
}
int listen(lua_State* state) { return create(state, true); }
int connect(lua_State* state) { return create(state, false); }
}

void add_transport_api(lua_State* state) {
    if (luaL_newmetatable(state, handle_type)) {
        const luaL_Reg methods[] = {{"close", close_handle}, {"__gc", close_handle},
            {"__close", close_handle}, {"status", snapshot}, {"send", send},
            {"receive", receive}, {nullptr, nullptr}};
        luaL_setfuncs(state, methods, 0);
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");
        lua_pushliteral(state, "Nexus transport handle");
        lua_setfield(state, -2, "__metatable");
    }
    lua_pop(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, quota_key);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        new (lua_newuserdatauv(state, sizeof(Quota), 0)) Quota{0};
        lua_setfield(state, LUA_REGISTRYINDEX, quota_key);
    } else lua_pop(state, 1);
    lua_pushcfunction(state, listen);
    lua_setfield(state, -2, "listen");
    lua_pushcfunction(state, connect);
    lua_setfield(state, -2, "connect");
}
