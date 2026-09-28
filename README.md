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
The first native checkpoint has [build and runtime test instructions](docs/bridge-loading.md).
See the [platform matrix](docs/platforms.md) for Windows, Linux and ARM limitations.
The [wire contract](docs/bridge-wire.md) describes the framing and bounded queues
under development for the transport worker.
The [two-server checkpoint](docs/bridge-transport.md) covers the 0.2.0 TCP candidate
and its required real-host lifecycle tests.
The [persistent listener checkpoint](docs/bridge-listener.md) covers 0.3.0 and
multiple connections on one transport port.

Licensed under the [MIT license](LICENSE). Copyright 2026 Yuketsu.
