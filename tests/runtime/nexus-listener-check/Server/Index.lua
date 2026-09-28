-- A (Proxy test instance): role="listen", address="0.0.0.0".
-- B (Game test instance): role="connect", address=A's reachable IPv4.
-- Use an allocated TCP port directed to A. No Proxy/Agent GameMode is required.
local role = "listen"
local address = "127.0.0.1"
local port = 25600

assert(role == "listen" or role == "connect", "Invalid listener test role")
assert(nexus_bridge.info().bridge_version == "0.3.0", "Install Bridge 0.3.0 binaries")
local listener = role == "listen" and assert(nexus_bridge.listener(address, port)) or nil
local peers = {}
local filler = string.rep("x", 1000) .. "\0binary"
local closed, on_tick = false, nil
local ticks, tick_sum, tick_max, work_max_ms = 0, 0, 0, 0
local function payload(label, index)
    return label .. ":" .. index .. ":" .. filler
end
local function entry(handle, label)
    return {handle = handle, label = label, sent = 0, received = 0,
        started = Server.GetTime(), complete = false, state = nil}
end
if role == "connect" then
    for i = 1, 2 do
        peers[i] = entry(assert(nexus_bridge.connect(address, port)), "client" .. i)
    end
end
local function dispose(peer, reason)
    local before = Server.GetTime()
    peer.handle:close()
    Console.Log("[Nexus listener] closed role=%s peer=%s reason=%s join_ms=%d",
        role, peer.label or "unidentified", reason, Server.GetTime() - before)
end
local function cleanup()
    if closed then return end
    closed = true
    Server.Unsubscribe("Tick", on_tick)
    -- Stop admission first; accepted sessions are independently owned.
    if listener then listener:close() end
    for _, peer in ipairs(peers) do dispose(peer, "local_close") end
    peers = {}
    Console.Log("[Nexus listener] cleanup complete role=%s", role)
end
local function process(peer)
    local state, reason = peer.handle:status()
    if state ~= peer.state then
        Console.Log("[Nexus listener] state=%s role=%s peer=%s reason=%s",
            state, role, peer.label or "unidentified", reason)
        peer.state = state
    end
    if state == "failed" or state == "closed" then return reason end
    if state == "connected" then
        for _ = 1, 4 do
            local bytes, why = peer.handle:receive()
            if bytes == nil then
                if why ~= "empty" and why ~= "busy" then return why end
                break
            end
            if not peer.label then
                local label = bytes:match("^(client[12]):")
                if not label then return "invalid_test_label" end
                peer.label = label
            end
            local sender = role == "listen" and peer.label or "server:" .. peer.label
            if peer.received >= 100 or bytes ~= payload(sender, peer.received + 1) then
                return "payload_or_order"
            end
            peer.received = peer.received + 1
        end
        if peer.label and peer.sent < 100 then
            local sender = role == "listen" and "server:" .. peer.label or peer.label
            local accepted, why = peer.handle:send(payload(sender, peer.sent + 1))
            if accepted then peer.sent = peer.sent + 1
            elseif why ~= "full" and why ~= "busy" then return why end
        end
        if not peer.complete and peer.sent == 100 and peer.received == 100 then
            peer.complete = true
            peer.report = true
        end
    end
    if not peer.complete and Server.GetTime() - peer.started > 30000 then return "test_deadline" end
end
local listener_state
on_tick = function(delta_time)
    local before = Server.GetTime()
    ticks = ticks + 1
    tick_sum = tick_sum + delta_time
    tick_max = math.max(tick_max, delta_time)
    -- Free completed/failed handles before admitting replacement connections.
    for i = #peers, 1, -1 do
        local reason = process(peers[i])
        if reason then
            if not peers[i].complete then
                Console.Log("[Nexus listener] INCOMPLETE role=%s peer=%s reason=%s",
                    role, peers[i].label or "unidentified", reason)
            end
            dispose(peers[i], reason)
            table.remove(peers, i)
        end
    end
    if listener then
        local state, reason, bound_port = listener:status()
        if state ~= listener_state then
            Console.Log("[Nexus listener] listener=%s reason=%s port=%d", state, reason, bound_port)
            listener_state = state
        end
        if state == "failed" or state == "closed" then cleanup(); return end
        -- One admission per Tick, and at most two sessions in this harness.
        if #peers < 2 then
            local handle, why = listener:accept()
            if handle then peers[#peers + 1] = entry(handle)
            elseif why ~= "empty" and why ~= "busy" then
                Console.Log("[Nexus listener] admission failed reason=%s", why)
                cleanup()
            end
        end
    end
    work_max_ms = math.max(work_max_ms, Server.GetTime() - before)
    for _, peer in ipairs(peers) do
        if peer.report then
            peer.report = false
            Console.Log("[Nexus listener] PASS role=%s peer=%s sent=100 received=100 tick_avg_s=%.6f tick_max_s=%.6f work_max_ms=%d",
                role, peer.label, tick_sum / ticks, tick_max, work_max_ms)
        end
    end
end
Package.Subscribe("Unload", cleanup)
Server.Subscribe("Stop", cleanup)
Server.Subscribe("Tick", on_tick)
