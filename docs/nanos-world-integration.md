# nanos world integration findings

Reviewed on 2026-09-27 against the official documentation and repositories.
The current documentation identifies a1.156.0; that is a research baseline, not a
tested Nexus compatibility claim. Subsequent maintainer tests on server 1.156.0
validated diagnostic native loading, calls, script/VM reload and process restart;
see the [checkpoint evidence](bridge-loading.md#evidence-and-next-gate). Networking
and native worker lifetime have not been validated in the host.

The first host attempt established that C Module loading requires the process
argument `--enable_unsafe_libs` on the tested server. The public C Module example
omits this prerequisite. The flag also enables restricted Lua OS/I/O facilities
for server Packages; it is part of the deployment trust boundary. That attempt
stopped before native loading and does not validate the ABI.

## Packaging and languages

The [Packages guide](https://docs.nanos-world.com/docs/core-concepts/packages/packages-guide)
supports one GameMode and multiple script Packages. Proxy will be a GameMode;
Agent will be a server script Package with a Bridge dependency. Server code stays
in `Server/`; only `Client/` and `Shared/` files are sent to clients. Secrets must
not be placed in either downloadable directory.

The [loading guide](https://docs.nanos-world.com/docs/core-concepts/packages/package-loading-and-lua-environment)
documents Lua 5.4, dependencies loading before dependents, and the GameMode loading
before the configured script Packages. Each Package has its own environment.
Nexus must not assume that backend GameModes can use Agent exports during their
initial execution unless they explicitly declare a dependency.

## Native boundary

The [C Module guide](https://docs.nanos-world.com/docs/core-concepts/packages/c-module)
documents server-side native Packages, CMake builds, Windows DLLs and Linux shared
libraries. Its API is explicitly unstable. The official
[example](https://github.com/nanos-world-modules/module-example) is C++17 with an
`extern "C"` Lua entry point and `luaL_newlib`; C Module does not require the
implementation language to be C.

The [SDK](https://github.com/nanos-world/module-sdk/tree/8bea6bc806507fe82ab08f742f9c209fb4bae8f7)
revision reviewed is `8bea6bc806507fe82ab08f742f9c209fb4bae8f7`. Its public header
reports Lua 5.4.9. Its Windows and Linux libraries are Git LFS objects, so plain
pointer files cannot be linked. Pin the revision and verify downloaded objects.
Do not assume that header version alone establishes the host's binary ABI.

The bundled Linux archive failed shared-library linkage because it is non-PIC.
Linux now builds the official Lua 5.4.9 sources with PIC, checking the archive hash
and every SDK header before compilation. Windows retains the SDK library.
See the [platform matrix](platforms.md) for source provenance, build checks and
the distinction between native x86_64 support and unvalidated ARM emulation.

Proposed Nexus boundary: a version query, owned transport handles, asynchronous
listen/connect/send/close operations, bounded polling and transport diagnostics.
These are future Nexus functions, not nanos world APIs. The first native slice
validated diagnostic loading on one host before introducing worker lifetime complexity.

Networking workers must own bytes and native resources only. They must never
retain a Lua state, Player object or Lua callback. Lua pulls bounded events on
its own execution path. Native queues must be bounded by count and bytes, including
pending connections and partial frames. Shutdown must stop and join workers before
native code is unloaded. The official SDK does not establish an arbitrary-thread
Lua scheduling facility or safe unload guarantees; real-host tests are required.

## Lifecycle and transfers

The [Server API](https://docs.nanos-world.com/docs/scripting-reference/static-classes/server)
documents `PlayerConnect(ip, account_id, name, steam_id)` before a Player entity
exists, and `PlayerDisconnect` with the same fields plus a reason. A connect
attempt cannot confirm arrival. `Start`, `Stop`, `Restart` and `Tick(delta_time)`
provide lifecycle hooks; Tick work must remain small. Shutdown event ordering
and native lifetime still need runtime verification.

The [Player API](https://docs.nanos-world.com/docs/scripting-reference/classes/player)
documents `GetAccountID()` as a string and server-side `Connect(ip, password?)`,
where the address is `IP:PORT`. It documents no successful-arrival return value.
Keep Account IDs as opaque strings and confirm transfers from the target's actual
presence. Verify Spawn/Ready and snapshot timing before implementing presence.

## Command arbitration is unresolved

The [Chat API](https://docs.nanos-world.com/docs/scripting-reference/static-classes/chat)
defines `Chat.Subscribe(event_name, callback)` and `PlayerSubmit(message, player)`.
Returning false suppresses the message. Neither this contract nor the
[Events guide](https://docs.nanos-world.com/docs/core-concepts/scripting/events-guide)
establishes callback priority or exclusive handling between Packages. Unsubscribe
affects the current Package only.

Consequently, guaranteed Nexus-first or GameMode-first handling of a shared
command name cannot currently be claimed for arbitrary unmodified GameModes.
This is an unresolved requirement/API gap, not a proven impossibility. Do not
patch another Package's handlers or invent a priority argument.

The smallest proposed adjustment is administrator-selected conflict-free aliases
or disabled conflicting commands, with cooperative arbitration optional for
GameModes that integrate it. This proposal changes the required conflict guarantee
and is not adopted without maintainer agreement. Native Bridge work is independent
of this decision. Runtime callback-order experiments may supply additional
evidence but cannot establish an undocumented portable guarantee.

## Exports, configuration and persistence

The [Package API](https://docs.nanos-world.com/docs/scripting-reference/static-classes/package)
offers `Package.Export(variable_name, value)`, `Package.Require(file_path,
force_load?)` and Load/Unload events. Export a controlled facade rather than mutable
registry tables. Verify each lifecycle API again in its implementation slice.

[Persistent Data](https://docs.nanos-world.com/docs/core-concepts/scripting/persistent-data)
is loaded into memory, updated through `SetPersistentData(key, value)` and written
later or on unload. `FlushPersistentData()` writes immediately. Use it for small
durable settings; player presence, heartbeats, transfers and active queues remain
volatile. The current guide places files under `Packages/.data/`; older/next
documentation differs, so Nexus should use the API rather than constructing paths.

[Server configuration](https://docs.nanos-world.com/docs/core-concepts/server-manual/server-configuration)
uses `Config.toml`. Nexus configuration parsing and validation are still to be
implemented; reload must validate a complete candidate before replacing live state.
The [HTTP API](https://docs.nanos-world.com/docs/scripting-reference/static-classes/http)
provides requests, including asynchronous requests. It is not a reason to replace
the required persistent native Bridge with an external service.

## Development environment

Initial inspection found Git 2.47.1, Git LFS 3.6.0, authenticated GitHub CLI,
CMake 4.1.2, Ninja 1.13.1, Clang 16, Visual Studio Build Tools 2026 with C++ tools,
and Windows SDKs. MSVC is not on the ordinary shell PATH; use a developer shell or
an explicitly configured generator. No standalone Lua interpreter or LuaRocks
was on PATH. Rust is installed but is not required by the chosen implementation.

Compiler discovery is not a successful build. Windows native compilation and
host loading remain separate checkpoints. Windows and Linux standalone CI tests
now pass; see the platform matrix and runtime evidence for actual coverage.
Do not publish support or performance claims from this inventory.
