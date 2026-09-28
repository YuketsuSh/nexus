"""Bundle the two verified x86_64 builds without copying the source checkout."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile


def bundle(windows, linux, revision, output):
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("revision must be a full Git commit hash")
    common = [
        "Packages/nexus-bridge/Package.toml",
        "Packages/nexus-bridge/licenses/LICENSE",
        "Packages/nexus-bridge/licenses/LICENSE.txt",
        "Packages/nexus-bridge-check/Package.toml",
        "Packages/nexus-bridge-check/Server/Index.lua",
        "Packages/nexus-transport-check/Package.toml",
        "Packages/nexus-transport-check/Server/Index.lua",
        "Packages/nexus-listener-check/Package.toml",
        "Packages/nexus-listener-check/Server/Index.lua",
    ]
    files = {}
    for name in common:
        data = (windows / name).read_bytes().replace(b"\r\n", b"\n")
        if data != (linux / name).read_bytes().replace(b"\r\n", b"\n"):
            raise ValueError(f"Platform builds disagree on {name}")
        files[name] = data

    dll_name = "Packages/nexus-bridge/nexus_bridge.dll"
    so_name = "Packages/nexus-bridge/libnexus_bridge.so"
    dll = (windows / dll_name).read_bytes()
    so = (linux / so_name).read_bytes()
    if len(dll) < 64 or dll[:2] != b"MZ":
        raise ValueError("Windows artifact is not a PE image")
    pe = struct.unpack_from("<I", dll, 60)[0]
    if (pe + 26 > len(dll) or dll[pe:pe + 4] != b"PE\0\0"
            or struct.unpack_from("<H", dll, pe + 4)[0] != 0x8664
            or struct.unpack_from("<H", dll, pe + 24)[0] != 0x20B):
        raise ValueError("Windows artifact must be x86_64 PE32+")
    if (len(so) < 64 or so[:6] != b"\x7fELF\x02\x01"
            or struct.unpack_from("<HH", so, 16) != (3, 62)):
        raise ValueError("Linux artifact must be an x86_64 ELF shared object")
    files[dll_name] = dll
    files[so_name] = so
    license_name = "Packages/nexus-bridge/licenses/lua-5.4.9-readme.html"
    files[license_name] = (linux / license_name).read_bytes()
    files["BUILD-INFO.json"] = (json.dumps({
        "revision": revision,
        "targets": ["windows-x86_64", "linux-x86_64"],
        "nanos_world_runtime_validation": "pending",
        "arm_emulation_validation": "pending",
    }, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n"
        for name, data in sorted(files.items())
    ).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    output.with_suffix(output.suffix + ".sha256").write_text(
        f"{hashlib.sha256(output.read_bytes()).hexdigest()}  {output.name}\n",
        encoding="utf-8",
    )
    print(f"Bundled {len(files)} files: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", required=True, type=Path)
    parser.add_argument("--linux", required=True, type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    bundle(args.windows, args.linux, args.revision, args.output)
