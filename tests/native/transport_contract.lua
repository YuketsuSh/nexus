-- Standalone Lua-state contract with real loopback sockets; not a fake host.
local function until_true(predicate)
    local deadline = os.clock() + 5
    repeat
        if predicate() then return end
        assert(os.clock() < deadline, "Lua transport test deadline exceeded")
    until false
end

assert(not pcall(nexus_bridge.listen, 1, 0))
assert(not pcall(nexus_bridge.listen, "127.0.0.1", -1))
assert(not pcall(nexus_bridge.connect, "127.0.0.1", 0))
assert(not pcall(nexus_bridge.connect, "127.0.0.1", 65536))
assert(not pcall(nexus_bridge.listen, "127.0.0.1", 0, "extra"))
local server = assert(nexus_bridge.listen("127.0.0.1", 0))
until_true(function() return server:status() == "listening" end)
local _, _, port = server:status()
assert(port > 0)
local client = assert(nexus_bridge.connect("127.0.0.1", port))
until_true(function() return client:status() == "connected" and server:status() == "connected" end)
assert(not pcall(client.send, client, 123))
local ok, reason = client:send(string.rep("x", 65537))
assert(not ok and reason == "invalid")

for _, payload in ipairs({"", "x\0y", string.rep("z", 65536)}) do
    for _, pair in ipairs({{client, server}, {server, client}}) do
        until_true(function()
            local sent, why = pair[1]:send(payload)
            assert(sent or why == "full" or why == "busy", why)
            return sent
        end)
        until_true(function()
            local received, why = pair[2]:receive()
            if received ~= nil then assert(received == payload); return true end
            assert(why == "empty" or why == "busy", why)
        end)
    end
end
client:close()
client:close()
assert(client:status() == "closed")
assert(not client:send("closed"))
until_true(function() return server:status() == "closed" end)
server:close()

local handles = {}
for i = 1, 16 do handles[i] = assert(nexus_bridge.listen("127.0.0.1", 0)) end
local extra, why = nexus_bridge.listen("127.0.0.1", 0)
assert(extra == nil and why == "handle_limit")
for _, handle in ipairs(handles) do handle:close() end
handles = nil
collectgarbage("collect")
do
    local scoped <close> = assert(nexus_bridge.listen("127.0.0.1", 0))
end
-- Leave workers reachable only through userdata: lua_close must finalize them
-- before the native test executable unloads the module library.
for _ = 1, 4 do assert(nexus_bridge.listen("127.0.0.1", 0)) end
