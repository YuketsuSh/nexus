-- Configure each isolated test server here. Only numeric IPv4 is supported.
-- A: listen on its private/local address; B: connect to A's reachable address.
local role = "listen" -- "listen" on A, "connect" on B
local address = "127.0.0.1"
local port = 7780 -- TCP transport port, separate from the nanos world game ports

assert(role == "listen" or role == "connect", "Invalid transport test role")
assert(nexus_bridge.info().bridge_version == "0.2.0", "Install Bridge 0.2.0 binaries")
local session = assert(nexus_bridge[role](address, port))
local peer_role = role == "listen" and "connect" or "listen"
local sent, received, ticks, busy = 0, 0, 0, 0
local tick_sum, tick_max, poll_max_ms = 0, 0, 0
local complete, closed = false, false
local last_state
local started = Server.GetTime()
local filler = string.rep("x", 1000) .. "\0binary"
local function payload(sender, sequence)
    return sender .. ":" .. sequence .. ":" .. filler
end
local on_tick
local function cleanup()
    if closed then return end
    closed = true
    local before = Server.GetTime()
    session:close()
    Server.Unsubscribe("Tick", on_tick)
    Console.Log("[Nexus transport] closed role=%s sent=%d received=%d join_ms=%d",
        role, sent, received, Server.GetTime() - before)
end
local function fail(reason)
    Console.Log("[Nexus transport] FAILED role=%s reason=%s", role, reason)
    cleanup()
end
on_tick = function(delta_time)
    local before = Server.GetTime()
    ticks = ticks + 1
    tick_sum = tick_sum + delta_time
    tick_max = math.max(tick_max, delta_time)
    local state, reason, bound_port = session:status()
    if state ~= last_state then
        Console.Log("[Nexus transport] state=%s reason=%s role=%s port=%d", state, reason, role, bound_port)
        last_state = state
    end
    if state == "failed" or state == "closed" then
        if not complete then fail(reason) else cleanup() end
        return
    end
    if state == "connected" then
        if sent < 100 then
            local accepted, why = session:send(payload(role, sent + 1))
            if accepted then sent = sent + 1
            elseif why == "full" or why == "busy" then busy = busy + 1
            else fail(why); return end
        end
        -- Explicit polling budget: at most eight diagnostic messages per Tick.
        for _ = 1, 8 do
            local bytes, why = session:receive()
            if bytes == nil then
                if why ~= "empty" and why ~= "busy" then fail(why); return end
                break
            end
            if received >= 100 or bytes ~= payload(peer_role, received + 1) then
                fail("payload_or_order"); return
            end
            received = received + 1
        end
    end
    poll_max_ms = math.max(poll_max_ms, Server.GetTime() - before)
    if not complete and sent == 100 and received == 100 then
        complete = true
        Console.Log("[Nexus transport] PASS role=%s sent=100 received=100 ticks=%d tick_avg_s=%.6f tick_max_s=%.6f poll_max_ms=%d backpressure=%d",
            role, ticks, tick_sum / ticks, tick_max, poll_max_ms, busy)
        -- Keep the connection/worker alive to test Package unload and server stop.
    elseif not complete and Server.GetTime() - started > 120000 then
        fail("test_deadline")
    end
end
Package.Subscribe("Unload", cleanup)
Server.Subscribe("Stop", cleanup)
Server.Subscribe("Tick", on_tick)
