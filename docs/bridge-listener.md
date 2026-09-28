# Bridge 0.3.0 persistent listener checkpoint

The 0.2.0 single-peer exchange and lifecycle gate passed on the maintainer's Linux
Pterodactyl instances (see [evidence](bridge-transport.md#automated-coverage-and-release-gate)).
Version 0.3.0 adds a persistent listener so a future Proxy can retain several
independent sessions and accept replacements without reopening its listening port.
This remains diagnostic transport: no Proxy GameMode, Agent, authentication, TLS,
business messages or automatic client reconnect is provided yet. Use isolated,
private/firewalled test instances. Windows real-host coverage remains pending.

## API and bounds

`nexus_bridge.listener(ipv4, port)` creates a handle asynchronously. Its
`status()` reports `starting`, `listening`, `failed` or `closed`, a reason and the
actual bound port. Numeric IPv4 and port rules match the existing
[`listen`/`connect` API](bridge-transport.md#lua-api-and-ownership). `listen` remains
the compatible one-peer convenience operation; it has not changed meaning.

`listener:accept()` returns one independently owned session handle, or
`nil, reason`: `empty`, `busy`, `closed`, `handle_limit` or `resource_error`.
It attempts the pending-queue mutex once and does no blocking socket I/O.
Successful acceptance allocates a session and starts its worker on the calling
thread, so it is not a hard real-time operation. Bound admissions per Tick.
Call `status`, `send`, `receive`, and `close` on accepted sessions as on connected
sessions. Their status port is the local listening port. `send`/`receive` on a
listener, or `accept` on a session, raises a Lua error.

The 16-live-handle limit is shared by listeners, outbound sessions, one-peer
listeners and accepted sessions in each Lua state. A listener plus 15 accepted
sessions therefore fills this diagnostic limit. It is a resource bound, not a
tested capacity claim. Explicitly close terminal handles to return their slots.

Each listener owns one worker and a fixed ring of eight unclaimed sockets. It
accepts at most one socket per 2-ms loop; an excess socket is closed without
allocating a session, payload queue, worker or Lua object. Unclaimed sockets expire
after 10 seconds, including when Lua never calls `accept`. No payload bytes are
read before acceptance. The requested kernel backlog is eight, but kernel buffer
and backlog behavior is OS-dependent and separate from the native ring bound.
At most 16 listeners per state can retain 128 unclaimed native sockets in total;
session queue limits from 0.2.0 remain in force.

After acceptance, ownership transfers to the session. Closing/collecting a
listener cancels and joins its worker and closes every unclaimed socket. It does
not close sessions already returned to Lua. Scripts must close both the listener
and all sessions on unload/stop. This independence is also enforced for the
Windows socket runtime: accepted sessions retain their own runtime reference.
All public operations belong on one Lua owner thread; worker code never calls Lua.

The fixed per-session framing, queue and I/O deadlines are unchanged. A malformed
peer affects only its session, not the listener or other sessions. No fairness,
source-IP rate limiting or authentication guarantee is implied; those need the
later authenticated registration layer. Connection labels in the test harness
are diagnostic assertions only and must not be used as server identities.

## Two-instance test

Stop both servers before replacing native binaries. Copy `nexus-bridge` and
`nexus-listener-check` from the successful candidate CI ZIP to both servers.
In each `Config.toml`, replace the previous `"nexus-transport-check"` entry with
`"nexus-listener-check"`; do not enable both harnesses on the same TCP port.
The optional `nexus-bridge-check` may remain. Keep existing GameModes and game/query
ports. Preserve `--enable_unsafe_libs` and the Linux `NanosWorldServer.sh` wrapper.

In `nexus-listener-check/Server/Index.lua`, A uses `role = "listen"`, binds its
private interface or `0.0.0.0`, and uses its allocated TCP transport port. B uses
`role = "connect"`, A's reachable numeric IPv4 and the same TCP port. Loopback is
valid only when the processes share a network namespace. The default port is
25600; allocate/forward the actual chosen port to A and restrict access to B.

1. Start A, then B. B opens two sessions to the same listener. Expect two PASS
   lines on each instance, one for `client1` and one for `client2`:

   ```text
   [Nexus listener] PASS role=listen peer=client1 sent=100 received=100 ...
   [Nexus listener] PASS role=listen peer=client2 sent=100 received=100 ...
   [Nexus listener] PASS role=connect peer=client1 sent=100 received=100 ...
   [Nexus listener] PASS role=connect peer=client2 sent=100 received=100 ...
   ```

2. On B only, run `package reload nexus-listener-check`. A must remain listening
   and accept both replacements without its own reload. Expect the four PASS
   lines again, plus closure logs for the old sessions.
3. Run `package reload all` on A, then B. Expect cleanup logs and a new PASS pair
   per instance. B does not reconnect itself when A is restarted; its reload or
   restart intentionally creates the replacement clients.
4. Stop B and verify its normal exit. Restart B while leaving A running; expect
   both exchanges to pass again. Repeat with abrupt termination of isolated B:
   A must remain running and accept B after it restarts.
5. Stop A first and confirm B survives the disconnection. Stop B normally. Restart
   A then B, verify both exchanges, and finish with normal server shutdowns.

Return PASS/closure/cleanup logs, whether A needed any unexpected restart, candidate
commit, server build and platform/container details. The test reports Tick deltas,
maximum callback work and join duration; millisecond wall-clock measurements are
coarse and include scheduling. They are not latency guarantees or load benchmarks.
The harness caps itself at two sessions and one admission per Tick. It does not
establish that two real Agents have registered or that players can transfer.

## Automated verification

Native tests use real sockets to verify simultaneous independent sessions,
replacement connections, malformed-peer isolation, continued traffic after
listener destruction, eight-slot pending admission, expiry without Lua polling,
recovery after saturation and cancellation with pending clients. Lua tests cover
mixed-handle quota enforcement, method types and GC/scoped/explicit cleanup across
20 Lua-state/module lifetimes. Existing single-peer tests remain regression gates.
Linux sanitizer runs include the listener; the host harness is syntax-checked.
Real nanos world multi-session reload/stop evidence is still required before this
candidate is considered runtime-validated.
