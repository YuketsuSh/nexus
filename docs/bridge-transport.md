# Bridge 0.2.0 transport checkpoint

This records the single-peer milestone. For the current 0.3.0 candidate and its
multi-session test, use the [listener checkpoint](bridge-listener.md). Its original
single-peer API remains compatible; the bundled older harness now checks 0.3.0.

This candidate adds real TCP transport to the native Bridge. It is a diagnostic
checkpoint, not a Proxy or Agent. One session accepts/connects one peer; there is
no multi-Agent acceptor, automatic reconnect, TLS, authentication or business
message handling. Use isolated instances on loopback or a private/firewalled
network. Do not expose the diagnostic listener publicly.

The Linux library now also requires the system C++ runtime (`libstdc++.so.6` and
`libgcc_s.so.1`); CI inspects its dependencies and tests the Ubuntu 22.04 build
on the documented Linux distributions. A newer local build is not a substitute
for that baseline artifact.

## Lua API and ownership

The existing `info()` and ABI 1 remain compatible; `bridge_version` is 0.2.0.
These are Nexus APIs, not built-in nanos world functions:

| Operation | Result |
| --- | --- |
| `nexus_bridge.listen(ipv4, port)` | Handle, or `nil, reason` for resource/quota failure. Port 0 selects an ephemeral local port. |
| `nexus_bridge.connect(ipv4, port)` | Handle, or `nil, reason`. Port must be 1..65535. |
| `session:status()` | State, reason, port. Read the chosen listening port after `listening`. |
| `session:send(bytes)` | `true, "ok"` when locally queued; otherwise `false, reason`. No remote delivery guarantee. |
| `session:receive()` | One binary-safe string and `"ok"`, or `nil, reason`. Empty strings are valid messages. |
| `session:close()` | Cancel and join the worker, release native memory; idempotent. Discards remaining queued sends/receives. |

Addresses must be numeric IPv4; there is no DNS resolution. Type/range errors
raise Lua errors; invalid numeric addresses are reported by the asynchronous
worker. States: `starting`, `listening`, `connecting`, `connected`, `closed`,
`failed`. Terminal reasons: `local_close`, `peer_closed`, `invalid_address`,
`socket_error`, `bind_error`, `connect_error`, `io_error`, `protocol_error`,
`timeout`, `resource_error`. `send` before connection returns `closed`; consult
`status` to distinguish pending from terminal sessions.

`send` returns `full` or `busy` for backpressure/contention and `invalid` above
64 KiB. `receive` returns `empty`, `busy` or `closed` when no message is available.
After remote termination, admitted receive messages remain drainable until
explicit close. Terminal handles must be closed before replacement. Creating a
handle is not evidence of a successful connection.

Each Lua state permits 16 live handles, including failed/closed-worker handles
not yet released. Each session owns one thread and one active socket; a listener
stops accepting after its first connection. Each direction has a 64-frame/1-MiB
queue (wire-byte accounting). One pending decoded frame, one encoded send,
temporary encoding data and a 4096-byte receive buffer are additional bounded
storage. OS buffers and thread stacks are separate. See the [wire contract](bridge-wire.md).

Workers own sockets and bytes only, never Lua states or callbacks. Socket calls
are nonblocking. Each loop attempts bounded I/O then waits up to 2 ms, with a wake
for cancellation; this is not a latency/throughput guarantee. Live queue operations
try their mutex once, with bounded copying and no wait for network progress.
Connection attempts, incomplete incoming frames (including a frame held by a full
receive queue) and blocked in-flight writes expire after 10 seconds. Idle sessions
have no heartbeat timeout yet. Disconnection may discard accepted but unwritten
sends; application acknowledgements are a later milestone.

Close is a lifecycle operation which joins, not a real-time Tick operation. It
waits for local worker cleanup and OS scheduling, never peer cooperation or the
10-second deadline. All handle calls belong on its Lua owner thread. `__gc` and
Lua 5.4 `__close` also join. Explicitly close on Package unload and server stop;
native unload ordering remains a runtime gate.

## Two-server procedure

Use the combined ZIP from the successful candidate CI run linked in the PR. Stop
both servers before replacing native binaries. Copy `nexus-bridge` and
`nexus-transport-check` from its `Packages/` into both servers. The older
`nexus-bridge-check` is optional and is not the transport test.

1. Append `"nexus-transport-check"` to `[game].packages` in each `Config.toml`.
   Preserve GameModes, maps and dependencies. Instances on one machine need
   separate server folders and distinct nanos world game/query ports.
2. Edit the first three settings in `Packages/nexus-transport-check/Server/Index.lua`.
   On A: `role = "listen"`, `address = "127.0.0.1"`, `port = 7780`.
   On B: `role = "connect"`, same address and port. This works for processes
   sharing the host network. Across machines/containers, A binds its reachable
   private interface (or `0.0.0.0`) and B uses A's reachable numeric IPv4. Allow
   only B to reach A's TCP transport port, separate from the nanos world game ports.
3. Start A then B within two minutes, preserving `--enable_unsafe_libs`.
   Linux: `./NanosWorldServer.sh --enable_unsafe_libs`.
   Windows: `.\NanosWorldServer.exe --enable_unsafe_libs`.
   Expect `connected` on both, followed by:

   ```text
   [Nexus transport] PASS role=listen sent=100 received=100 ...
   [Nexus transport] PASS role=connect sent=100 received=100 ...
   ```

   Each side checks contents, embedded zeros and order. The harness sends at most
   one message and polls at most eight messages per Tick. It keeps successful
   sessions alive so cleanup tests exercise running workers.
4. Run `package reload nexus-transport-check` on A, then B. A starts a fresh
   one-peer listener and B creates a new connection. Expect cleanup logs and
   another PASS pair. Repeat in that order with `package reload all`. The other
   side may report peer closure during this step.
5. Stop B while connected, then A. Verify normal process exits and cleanup logs.
   Restart A then B and verify another PASS pair. Also try stopping A first:
   B must observe termination without crashing its GameMode. Restart both for
   further tests; this checkpoint intentionally does not reconnect itself.
6. For abrupt loss, kill B's isolated process after PASS. A must observe peer
   closure/I/O failure and stay running. Stop A normally afterward. Do not run
   this step on a shared production instance.

Report the commit, nanos world build, OS versions, process architectures,
container/network layout, startup/PASS/cleanup logs, and reload/stop results.
The harness records average/maximum Tick delta in seconds, maximum polling
callback duration and join duration in milliseconds. `Server.GetTime` is a wall
clock with millisecond granularity, not a microbenchmark clock. These figures
include scheduling/logging: compare an equivalent run without the harness before
attributing tick changes to the Bridge. No capacity/performance claim is made.

## Automated coverage and release gate

On 2026-09-28 the maintainer supplied nanos world 1.156.0 logs from two Linux
Pterodactyl instances. Bridge 0.2.0 completed 100 validated messages in each
direction over the allocated TCP transport port. A reported 1467 ticks (including
its wait for B), average 0.033333 s, maximum 0.035741 s; B reported 100 ticks,
average 0.033334 s, maximum 0.034404 s. Both reported maximum callback duration
1 ms and zero backpressure. These light-load observations are not capacity
benchmarks or a controlled baseline comparison. Addresses and server identifiers
are omitted from public evidence.

The maintainer also confirmed successful script/whole-VM reload, orderly
stop/restart and abrupt B termination without problems. Those lifecycle outcomes
are maintainer-reported; only the initial exchange logs were supplied. This closes
the 0.2.0 single-peer host gate on that environment, not Windows host validation,
ARM support, or the next multi-connection listener's lifecycle gate.

Real loopback tests cover bidirectional binary/max-size messages, FIFO,
fragmented/coalesced input, malformed/truncated streams, partial-frame timeouts,
refused connections, bind collisions, backpressure, stalled receive queues and
repeated worker cancellation. Lua tests cover quotas, explicit/scoped/GC cleanup
and native module unload across 20 lifetimes. The host harness is syntax-checked;
standalone tests do not emulate nanos world events.

CI checks Windows/Linux, three Linux distribution containers, native worker
memory sanitizers and artifacts. The single-peer host gate passed as recorded
above. Windows and Linux host validation are separate evidence; cross-platform
host safety cannot be inferred from CI.

The harness uses documented [Server Tick/Stop/GetTime](https://docs.nanos-world.com/docs/scripting-reference/static-classes/server)
and [Package Unload](https://docs.nanos-world.com/docs/scripting-reference/static-classes/package).
