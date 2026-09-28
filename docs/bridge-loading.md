# Bridge native loading checkpoint

This milestone builds the real `nexus_bridge` C Module entry point and its
`info()` diagnostic. It contains no networking, authentication or player logic.
It is the first prerequisite for the transport proof, not a usable network Bridge.

## Build

Requirements: Git with Git LFS, CMake 3.24 or newer and x86_64 C99/C++17 compilers.
On Windows use Visual Studio C++ Build Tools with a Windows SDK. On Linux use GCC
or Clang and the system C/C++ development libraries. The official SDK is pinned as
a submodule. Linux configuration downloads the official Lua 5.4.9 source archive,
verifies its published SHA-256 and compares every SDK header with the source.
It builds a private static Lua library with position-independent code (PIC).
Windows continues to use the pinned SDK's verified library. Neither target needs
a separately installed Lua runtime. See [platforms](platforms.md) for the support limits.

From the repository root:

```sh
git submodule update --init --recursive
git -C bridge/deps/module-sdk lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix stage
```

For Ninja on Windows, run these commands in an x64 Visual Studio Developer
PowerShell and add `-G Ninja` to the configure command. A build directory belongs
to one generator/toolchain. Use a different build directory when changing either.

Configuration rejects an unexpected SDK revision, Windows LFS pointer/library
hash mismatch, Linux source archive hash mismatch or source/SDK header mismatch.
Lua sources and checksums come from [lua.org](https://www.lua.org/ftp/).
The installed DLL uses the Microsoft C runtime;
a missing `VCRUNTIME140.dll` indicates a runtime dependency problem.

## Current native contract

`nexus_bridge.info()` accepts no arguments and returns a fresh table:

| Field | Meaning |
| --- | --- |
| `abi_version` | Nexus native surface version, currently 1 |
| `bridge_version` | Component version, currently 0.1.0 |
| `lua_headers` | SDK header release used at build time |
| `linked_lua_api_version` | Lua API version from the linked SDK, currently 504 |
| `sdk_revision` | Pinned official SDK commit |
| `pointer_bits` | Native pointer width, currently 64 |

These fields do not discover the host's Lua patch version or prove compatibility
with it. The upstream example links Lua into the module; Linux rebuilds that same
Lua version with PIC instead of linking the unusable upstream archive.
Only `luaopen_nexus_bridge` is intentionally exported. There are no
workers, sockets, persistent native resources or mutable process-global state yet.

The standalone test dynamically loads the compiled library into a Lua state made
with the same target's Lua library. It checks the diagnostic contract, invalid arguments, independent
returned tables, garbage collection and 20 load/state lifetimes. It also compiles
the runtime harness for Lua syntax without executing nanos world APIs. It cannot
validate the real host's loader or lifecycle.

## One-server runtime procedure

Use the `nexus-bridge-windows-linux-x86_64` artifact from the candidate's successful
CI run. Unpack its inner ZIP: it contains both `nexus_bridge.dll` and
`libnexus_bridge.so` in the same Package, plus a `BUILD-INFO.json` recording the
commit and `SHA256SUMS`. nanos world selects the library for the current OS.
Do not substitute an artifact from a failed run or another commit.
When building locally, `stage/` contains only the platform just built.

Use branch `feat/bridge-native-loading` at the candidate commit identified in the
PR. One isolated Windows x64 or Linux x86_64 nanos world server is sufficient; no players,
credentials, Proxy or second server are needed. Record the server version from
the startup log. Keep an existing unrelated GameMode and its map/dependencies.

1. Stop the test server. Copy both directories from the artifact's `Packages/` into its
   `Packages/` directory:
   - `nexus-bridge/`: `Package.toml`, both native libraries and `licenses/`.
   - `nexus-bridge-check/`: `Package.toml` and `Server/Index.lua`.
2. In the server's existing `Config.toml`, append `"nexus-bridge-check"` to
   `[game].packages`. Preserve existing entries and `game_mode`. The harness
   declares `nexus-bridge` as a dependency, so no second list entry is necessary.
   No Nexus address, secret, port or firewall change is needed for this test.
3. Add `--enable_unsafe_libs` to the server's launch arguments, preserving any
   existing arguments. For a direct PowerShell launch from the server directory:

   ```powershell
   .\NanosWorldServer.exe --enable_unsafe_libs
   ```

   On Linux, launch through the official wrapper from the server directory:

   ```sh
   ./NanosWorldServer.sh --enable_unsafe_libs
   ```

   This is a process startup flag, not a console command or a Package setting.
   The tested server refuses C Modules without it. It also enables otherwise
   restricted Lua OS and I/O functions for server Packages, so use trusted
   Packages on the isolated test instance. Start the server with this flag on
   subsequent restarts too. Expect these message bodies (the server adds
   its own log prefix), with no assertion or native-loader error:

   ```text
   [Nexus check] loaded bridge=0.1.0 abi=1 headers=Lua 5.4.9 linked_api=504 bits=64
   [Nexus check] completed 100 Lua-driven native calls
   ```

   The second message should appear after about 100 server ticks, approximately
   three seconds at the documented default tick rate. This is a functional
   call check, not a latency measurement or a thread-safety proof.
4. In the server console run `package reload nexus-bridge-check`. Expect
   `[Nexus check] script unloading after 100 ticks`, followed by the two startup
   messages again. This tests script reload with the native dependency loaded.
5. Run `package reload all` on this isolated server. Expect another completed
   100-call cycle, no crash and normal GameMode reload. This tests the documented
   Lua VM restart path; it does not prove that the host unloads the DLL itself.
6. Run `stop`, verify the process exits normally, then start it again and wait
   for another completed cycle. Stop the server when finished.

Send back the candidate commit, OS/distribution, CPU architecture, container or
emulation details and nanos world version, the startup
through shutdown console log, and whether both reload commands and the restart
completed. If any step crashes, stop testing and include the last log messages
and crash report. If the Bridge global is missing, the entry point cannot load,
an assertion fails, or the 100-call message never appears, report that step.
Do not send unrelated configuration secrets.

The harness uses documented
[Server Subscribe/Unsubscribe and Tick](https://docs.nanos-world.com/docs/scripting-reference/static-classes/server),
[Package Unload](https://docs.nanos-world.com/docs/scripting-reference/static-classes/package)
and [Console.Log](https://docs.nanos-world.com/docs/scripting-reference/static-classes/console).
The console commands follow the
[server manual](https://docs.nanos-world.com/docs/core-concepts/server-manual/server-configuration).

## Evidence and next gate

Local Windows Release compilation and the standalone CTest contract have passed
using MSVC 19.51.36257, CMake 4.1.2 and the pinned SDK. Export inspection found
`luaopen_nexus_bridge` as the sole DLL export. Installation staging was exercised.
The first [CI run](https://github.com/YuketsuSh/nexus/actions/runs/36345525857)
passed the Windows build, contract test and installation steps. Linux failed
while linking the shared module: the pinned SDK's `liblua.a(lauxlib.c.o)` contains
`R_X86_64_PC32` relocations against `stderr`; the linker requires a PIC rebuild.
That linkage failure is addressed by rebuilding the matching official Lua sources
with PIC after comparing all SDK headers. Ubuntu 24.04 WSL compilation and the
standalone contract have now passed locally. No host-exported Lua symbols or
system Lua installation are assumed. CI builds on Ubuntu 22.04 and exercises that
same Linux artifact inside Ubuntu 22.04, Ubuntu 24.04 and Debian 13 containers;
consult the candidate's actual check results. These are library tests, not real
nanos world server tests.

The first real-server attempt refused the C Module because the startup flag was
missing; the harness then reported that the Bridge global was unavailable.
The server also inserted omitted manifest fields, including the confirmed string
`lua_version = "5.4"` under `[c_module]`. Both manifests now include those fields.
The script's `compatibility_version = "1.156"` selects the documented scripting
baseline, independently of the native Lua version. These results do not establish
native ABI compatibility: the module had not been loaded. The script manifest now
includes the required fields, and the startup procedure includes the required flag.

The second host attempt progressed past the unsafe-library gate but requested
`Packages/nexus-bridge/libnexus_bridge.so`, revealing that the test host uses the
Linux loader. The supplied local artifact was Windows-only. It failed before
loading native code, so neither attempt validates the ABI or reload behavior.
The combined candidate now supplies both platform libraries after successful CI
checks. Record the host distribution/version, process architecture, container
environment and nanos world build when running the next test.

On 2026-09-28 the maintainer supplied a startup screenshot from nanos world
1.156.0 (Unreal 5.7.4). It shows the native module loading, the diagnostic
reporting Bridge 0.1.0, ABI 1, Lua headers 5.4.9, linked API 504 and 64-bit
pointers, then completing 100 Lua-driven native calls. Other GameMode Packages
also loaded in that run. This confirms startup and the diagnostic call boundary
on that host; it does not validate transport or GameMode integration beyond startup.
The test follows the Linux artifact correction, but the screenshot does not
identify the distribution, process architecture or artifact commit. Those
environment details still need confirmation. The screenshot is not committed
because it contains unrelated server identifiers.

The maintainer subsequently supplied logs showing successful script reload and
`package reload all`, each followed by another completed 100-call cycle. The
whole-VM reload reported a single 71.37 ms tick while reloading all Packages;
this is not a measurement of Bridge overhead. Clean process shutdown and a full
restart with another completed cycle were also confirmed by the maintainer.
This closes the diagnostic-only lifecycle checkpoint on that test host. It does
not establish worker shutdown safety: the diagnostic module has no workers yet.
Windows host loading and ARM execution under emulation are also unvalidated.
PR #2 was merged by the maintainer. The next milestone adds
bounded transport and tests two server processes, worker shutdown, Lua polling,
slow peers and tick impact. That proof still blocks inter-server product features.
