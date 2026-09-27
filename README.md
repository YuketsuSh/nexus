# Nexus

Proxy and server network framework for nanos world.

Nexus is being developed to coordinate player presence, routing, transfers and
network permissions across multiple nanos world servers. A dedicated Proxy
GameMode coordinates the network; an Agent script Package runs alongside each
backend's existing GameMode; a native Bridge carries server-to-server messages.
Player connections move to the backend after redirection.

The project is in its foundation phase. No installable network implementation
or supported runtime version is available yet. Native loading and transport must
be validated in real nanos world servers before network features proceed.

Start with the [development roadmap](docs/roadmap.md) and
[verified integration constraints](docs/nanos-world-integration.md).
See [contributing](CONTRIBUTING.md) for the development workflow.

Licensed under the [MIT license](LICENSE). Copyright 2026 Yuketsu.
