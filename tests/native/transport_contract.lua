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

local listener = assert(nexus_bridge.listener("127.0.0.1", 0))
until_true(function() return listener:status() == "listening" end)
local _, _, listener_port = listener:status()
assert(not pcall(listener.send, listener, "wrong handle"))
assert(not pcall(listener.receive, listener))
local peers, sessions = {}, {}
for i = 1, 3 do
    peers[i] = assert(nexus_bridge.connect("127.0.0.1", listener_port))
    until_true(function()
        local handle, why = listener:accept()
        if handle then sessions[i] = handle; return true end
        assert(why == "empty" or why == "busy", why)
    end)
    until_true(function() return peers[i]:status() == "connected" and sessions[i]:status() == "connected" end)
    assert(not pcall(sessions[i].accept, sessions[i]))
end
listener:close()
assert(select(2, listener:accept()) == "closed")
for i = 1, 3 do
    until_true(function() return peers[i]:send("peer:" .. i) end)
    until_true(function()
        local bytes = sessions[i]:receive()
        if bytes then assert(bytes == "peer:" .. i); return true end
    end)
    peers[i]:close()
    sessions[i]:close()
end

-- The 16-handle quota includes listeners and the sessions they return.
local limited = assert(nexus_bridge.listener("127.0.0.1", 0))
local fillers = {}
for i = 1, 15 do fillers[i] = assert(nexus_bridge.listen("127.0.0.1", 0)) end
assert(select(2, limited:accept()) == "handle_limit")
for _, filler in ipairs(fillers) do filler:close() end
limited:close()

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
assert(nexus_bridge.listener("127.0.0.1", 0))
