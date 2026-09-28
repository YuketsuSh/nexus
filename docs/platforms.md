# Native platform compatibility

Nexus distributes one archive containing distinct Windows and Linux x86_64
libraries. A single native binary cannot use both the Windows PE and Linux ELF
loading formats. The library must match the architecture of the **server process**,
which can differ from the host CPU when emulation is used.

| Target | Build and validation policy |
| --- | --- |
| Windows x86_64 | Pinned nanos world SDK library; Windows CI build, dynamic-load contract and installation checks. Real server validation pending. |
| Linux x86_64 | Official Lua 5.4.9 rebuilt with PIC; all SDK headers checked against upstream sources. CI builds on Ubuntu 22.04 and runs the same binary on Ubuntu 22.04, Ubuntu 24.04 and Debian 13 containers. Real server validation pending. |
| ARM/ARM64 host running an emulated x86_64 server | Use the x86_64 library for the server's OS. Emulation is experimental and unvalidated by Nexus. |
| Native ARM/ARM64 server process | No verified official nanos world server/SDK target. Native Nexus artifacts are not published or claimed compatible. |
| Other Linux distributions | Requires compatible x86_64/glibc and nanos world dependencies; distribution names alone do not establish binary compatibility. No universal guarantee. |

The [official installation guide](https://docs.nanos-world.com/docs/core-concepts/server-manual/server-installation)
lists Ubuntu 22.04, Ubuntu 24.04 and Debian 13 and describes a glibc 2.35 server
build baseline. Nexus builds its CI Linux library on Ubuntu 22.04 to avoid raising
that baseline accidentally. Dependency and symbol-version inspection is included
in the Linux job logs. Container tests load the library and run its contract;
they do not launch nanos world or validate gameplay.

The [official ARM guide](https://docs.nanos-world.com/docs/core-concepts/server-manual/server-linux-arm)
explicitly marks ARM unsupported and its community emulation instructions as
experimental and possibly outdated. A native ARM Bridge cannot make an x86_64
server run natively on ARM. Windows ARM emulation has no verified Nexus runtime
evidence either. Native ARM support requires a matching server and SDK first.

## Linux library provenance

The SDK commit is pinned in Git and checked by CMake. Its bundled Linux static
library fails shared-library linkage because it was not built with PIC. Nexus
therefore rebuilds Lua 5.4.9 from the
[official source archive](https://www.lua.org/ftp/), pinned by SHA-256
`2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`.
CMake compares all SDK `.h` files with upstream headers after normalizing line
endings and stops on any mismatch. The source license accompanies the artifact.
This preserves the Lua C boundary while correcting the Linux build options;
it does not prove the host's internal Lua ABI, which still needs the loading test.

## Artifacts

CI publishes each platform's staging directory and, only when both builds and
their checks succeed, `nexus-bridge-windows-linux-x86_64`. The combined ZIP includes
both libraries under `Packages/nexus-bridge`, the diagnostic script Package,
licenses, the source commit, per-file hashes and an external archive hash.
The bundler verifies PE/ELF target architecture, rejects differing common files
(apart from line endings) and copies only explicitly allowed files.

Use the [runtime procedure](bridge-loading.md) with the exact artifact commit.
Report the server build, OS version, architecture and any container/emulation
layer. Support claims will be updated from those results, not from compilation alone.
