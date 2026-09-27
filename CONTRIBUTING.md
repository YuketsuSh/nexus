# Contributing

Nexus is developed in small, reviewable milestones. Check the
[roadmap](docs/roadmap.md) before starting work. Production code must use verified
nanos world APIs; test doubles belong only in tests.

Use a branch for each coherent milestone and focused commits. Before committing
or pushing, inspect the staged and full branch diffs, run applicable checks and
verify that no secrets, private material, generated binaries or unfinished
production behavior are included. Preserve the configured Git identity. Do not
add co-author or AI attribution trailers. Project artifacts are written in English.

`internal-docs/` is strictly local material. Never track or force-add it, copy its
contents into public artifacts, or include it in release archives. Public
documentation must be written independently.

Run these repository checks from the root:

```sh
git diff --check
git diff --cached --check
git check-ignore internal-docs/privacy-check.txt
git ls-files -- internal-docs
```

The ignore check must succeed; the tracked-file check must print nothing.
Inspect `git diff --cached` before committing and `git diff origin/main...HEAD`
before opening a PR (use the actual base for a stacked PR).

Describe validation precisely: unit tests, local integration tests, native builds
and real nanos world runtime tests are separate evidence. A native build does not
prove host compatibility. Runtime-dependent PRs remain unmerged until the focused
test procedure has been run and the results recorded.

Proxy and Agent code will use Lua 5.4. Bridge code will use C++17 behind a Lua C
ABI, with CMake and CTest. The official module SDK must be pinned and its Git LFS
libraries materialized. No external service is required by the intended runtime.
Build instructions will be added with the first native milestone.
