# Development roadmap

These are planned milestones, not available features. Each row represents a
reviewable branch and PR, with tests and documentation delivered alongside the
behavior. The Bridge proof is a hard dependency: inter-server product features
must wait for successful real-host transport validation.

| Order | Branch | Scope and exit evidence |
| --- | --- | --- |
| 0a | `chore/repository-foundations` | Protect private material, record official API findings and establish this plan. Review Git history and public documentation. |
| 0b | `feat/bridge-native-loading` | Pin the official SDK, build the native entry point, report the actual ABI and provide a small host diagnostic Package. Validate loading, initialization and unload in nanos world before adding transport. |
| 0c | `feat/bridge-transport` | Bidirectional transport, bounded byte/message queues, nonblocking Lua boundary, shutdown and error handling. Test framing, fragmentation, limits, slow peers and concurrency. Prove communication between two real server processes and measure tick impact. |
| 1a | `feat/authenticated-registration` | Define NXP framing and payload schemas, versions, request IDs, nonce/timestamp proof and replay limits. Complete Agent-to-Proxy authentication, registration, identity ownership and duplicate rejection. Require successful authentication and synchronization before routing eligibility. |
| 1b | `feat/server-recovery` | Heartbeats, explicit server and connection state machines, timeouts, capped reconnect backoff with jitter, full synchronization and degraded behavior. Validate Proxy restart without disturbing backend gameplay. |
| 2 | `feat/player-presence` | Account ID index, normalized-name index, local join/leave events, snapshots, reconciliation, UNKNOWN grace period and duplicate-session policy. Validate real player lifecycle and reconnect recovery. |
| 3a | `feat/network-permissions` | Central Account ID assignments, groups, inheritance, wildcards and explicit denies; persistent configuration, Agent cache invalidation and centrally checked sensitive requests. Deliver before exposing privileged transfers or commands. |
| 3b | `feat/player-transfers` | Primary server/group resolution, fallback loop prevention, capacity reservations, transfer IDs and timeouts, target arrival confirmation and source execution. Validate initial redirection and backend-to-backend transfer. |
| 4 | `feat/network-commands` | `/server`, `/find`, `/glist`, `/send`, `/nexus`, aliases and console administration. Batch mass sends, audit privileged actions and test parsing/authorization. Resolve the command-priority API gap before implementing conflict policy. |
| 5 | `feat/routing-and-queues` | Dynamic groups; PRIORITY, ROUND_ROBIN, LEAST_PLAYERS, RANDOM, WEIGHTED and FILL; network/group/server maintenance, draining, visibility and restrictions; bounded FIFO queues with permission priorities and revalidation. |
| 6a | `feat/configuration-and-diagnostics` | Atomic reload with rollback and explicit restart-only fields, appropriate persistence, structured redacted logs, correlation IDs, diagnostic commands and internal metrics. |
| 6b | `test/network-failure-recovery` | Extend security and reliability tests for hostile Agents, per-source capabilities, rate limits, replay, oversized data, backend crash, Proxy loss, transfer timeout and full reconciliation. Security bounds start in earlier slices, not here. |
| 7 | `feat/extension-api` | Versioned controlled Lua API, immutable registry views, cancellable routing/transfer contracts, commands, routing strategies and documented event ordering. Rate-limit custom events routed by the Proxy to instances, groups or the network. |
| 8 | `docs/release-validation` | Installation and operations guides, tested compatibility matrix, migrations, measured load results and complete acceptance evidence. No 1.0 release until every runtime gate passes. |

## Ownership and layout

The Proxy owns global policy and volatile network state. Agents report the players
actually present locally. The Bridge owns transport only. Extensions use the Lua
API and never reach into native transport or registry tables.

Create directories when their implementations land: `bridge/` for native source,
`packages/` for deployable nanos world Packages, `tests/` for executable tests and
runtime harnesses, and `docs/` for public contracts and administration. Shared
contracts must have one authoritative definition. Do not create empty feature
packages in advance.

## Validation gates

The native diagnostic's startup, script/VM reload and process restart checkpoint
has passed on the maintainer's 1.156.0 host; platform details and Windows host
coverage remain limited as recorded in `bridge-loading.md`. The transport branch
first delivers independently tested framing and bounded native queues (see
`bridge-wire.md`). Sockets, worker lifetime and Lua polling follow as a separate
reviewable slice; the full two-server gate remains mandatory.

1. One isolated server: native module load, ABI diagnostics, initialization and
   cleanup. Record server build, platform, SDK revision and compiler.
2. Two servers: bidirectional Bridge messages, bounded polling, malformed frames,
   slow/disconnected peers, package reload and clean process shutdown. Repeat on
   Windows and Linux before claiming support for both.
3. Proxy and unrelated backend GameMode: Agent coexistence, handshake, rejection
   paths, Proxy restart, reconnect and mandatory synchronization.
4. Real players: Account IDs, loading/disconnect ordering, join/leave snapshots,
   initial Proxy redirection, `/server`, remote `/send` and target confirmation.
5. One Proxy and three backends across multiple groups: full/maintenance targets,
   fallbacks, queues, draining, backend crash, transport loss, duplicate sessions,
   degraded commands and post-restart reconciliation.

Each runtime PR will provide exact installation files, configuration, startup
order, actions, expected logs and requested failure evidence. Test success is
recorded separately from unit tests and compilation.

## Acceptance coverage

The release gate covers entry through a valid primary; authorized backend
navigation and remote administration; accurate reconciled presence; removal of
failed destinations; uninterrupted backend gameplay during Proxy loss; consistent
network permissions across GameModes; Agent installation without GameMode edits;
clear compatibility/configuration failures; bounded defensive transport; and no
external runtime infrastructure.

Additional required coverage includes transfer and player state machines,
permission inheritance and denies, aliases and ambiguity, queue cleanup and
priority, batching, routing strategies, atomic configuration changes and security
limits. Benchmarks must record build, environment, Agent/player counts, message
rate, CPU, memory, queue sizes and latency. Capacity claims follow measurements.

The public API must cover servers, groups, players, permissions, routing,
transfers, commands, events and connection state. Its event contract includes
server connection/state changes, player connection/server changes, transfer
requests/results, permission changes and Proxy connection changes.

Party, friends, advanced global chat, matchmaking and optional external metrics
integrations are post-1.0 work. They must use the public API.
