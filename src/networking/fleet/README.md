# Fleet driver and host setup

Start at `fleet.cppm` for peer driver; `fleet_setup.cppm` for authoritative composition.

The peer driver coordinates a fixed roster over authenticated transport and durability. Separate server-only setup/runtime code owns prepared bundles and local-session composition.

Allowed dependencies: Driver: persistence, wire and transport. Setup: local session, wire, driver and blob storage. Client/UI targets must not link setup.

Ownership and lifetime: Membership identity and committed frame application remain stable across recovery. Setup bundles retain exact pack/member credentials and are not public client content.

Relevant validation: `system.fleet-setup`, `system.fleet-runtime`, fleet recovery service cases. These identify checks to run; this README is not execution evidence.
