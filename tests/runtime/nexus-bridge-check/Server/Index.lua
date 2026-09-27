assert(type(nexus_bridge) == "table", "Nexus Bridge global is unavailable")
local info = nexus_bridge.info()
assert(info.abi_version == 1, "Unexpected Nexus Bridge ABI")
assert(info.pointer_bits == 64, "Expected a 64-bit Nexus Bridge")
assert(info.sdk_revision == "8bea6bc806507fe82ab08f742f9c209fb4bae8f7",
    "Unexpected Nexus Bridge SDK revision")

Console.Log("[Nexus check] loaded bridge=%s abi=%d headers=%s linked_api=%d bits=%d",
    info.bridge_version, info.abi_version, info.lua_headers,
    info.linked_lua_api_version, info.pointer_bits)

local ticks = 0
local on_tick
on_tick = function()
    assert(nexus_bridge.info().abi_version == 1, "Bridge call failed during Tick")
    ticks = ticks + 1
    if ticks == 100 then
        Server.Unsubscribe("Tick", on_tick)
        Console.Log("[Nexus check] completed 100 Lua-driven native calls")
    end
end
Server.Subscribe("Tick", on_tick)

Package.Subscribe("Unload", function()
    Console.Log("[Nexus check] script unloading after %d ticks", ticks)
end)
