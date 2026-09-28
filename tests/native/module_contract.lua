assert(type(nexus_bridge) == "table")
assert(type(nexus_bridge.info) == "function")
local info = nexus_bridge.info()
assert(info.abi_version == 1)
assert(info.bridge_version == "0.3.0")
assert(info.lua_headers == "Lua 5.4.9")
assert(info.linked_lua_api_version == 504)
assert(info.sdk_revision == "8bea6bc806507fe82ab08f742f9c209fb4bae8f7")
assert(info.pointer_bits == 64)

info.abi_version = -1
assert(nexus_bridge.info().abi_version == 1, "caller mutated module state")
for _, argument in ipairs({false, 1, "unexpected", {}, function() end}) do
    local ok, message = pcall(nexus_bridge.info, argument)
    assert(not ok and message:find("expects no arguments", 1, true))
end
assert(not pcall(nexus_bridge.info, nil))
for _ = 1, 1000 do
    assert(nexus_bridge.info().pointer_bits == 64)
end
collectgarbage("collect")
assert(nexus_bridge.info().abi_version == 1)
