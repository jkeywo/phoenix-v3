# C++20 module ownership

Phoenix's first-party public interfaces are named C++20 modules. Import them by name; the previous public `include/phoenix/*.hpp` headers have been removed. The binaries still link static libraries. Third-party dependencies retain their normal headers and pinned versions.

## Interfaces and dependencies

| Modules                                              | Owning target            | Responsibility                                                                                |
| ---------------------------------------------------- | ------------------------ | --------------------------------------------------------------------------------------------- |
| `phoenix.input`, `phoenix.contracts`, `phoenix.data` | `phoenix_foundation`     | Console routing, shared command rules, JSON/bytes, hashes, bounded file IO and secure tokens. |
| `phoenix.content`                                    | `phoenix_content`        | Packs, catalog verification, behavior trees and Workshop documents.                           |
| `phoenix.simulation`                                 | `phoenix_simulation`     | Authoritative world; Flecs and collision helpers remain internal.                             |
| `phoenix.transport`                                  | `phoenix_transport`      | Opaque native/browser peer links.                                                             |
| `phoenix.wire`                                       | `phoenix_wire`           | Authenticated packets and bounded content transfer.                                           |
| `phoenix.blobs`                                      | `phoenix_blobs`          | Asynchronous persistent byte storage.                                                         |
| `phoenix.replication`                                | `phoenix_replication`    | Transport-independent consensus actor and durable transactions.                               |
| `phoenix.persistence`                                | `phoenix_persistence`    | Native/browser peer journals and their shared consensus durability coordinator.               |
| `phoenix.fleet`                                      | `phoenix_fleet`          | Authenticated fixed-roster driver, without simulation.                                        |
| `phoenix.session.api`                                | `phoenix_session_api`    | Shared session interface and setup/network options, without authority.                        |
| `phoenix.session.local`                              | `phoenix_session`        | Local session factory and server-only `HostAccess`.                                           |
| `phoenix.network.host`                               | `phoenix_network_host`   | Network host factory and implementation.                                                      |
| `phoenix.network.client`                             | `phoenix_network_client` | Network client/GM factory and implementation.                                                 |
| `phoenix.fleet_setup`                                | `phoenix_fleet_setup`    | Server-only prepared fleets, bundles and durable runtime.                                     |
| `phoenix.application`, `phoenix.navigation`          | `phoenix_core`           | Application state, translation and permitted navigation.                                      |
| `phoenix.presentation`                               | `phoenix_presentation`   | Screens and native/browser accessibility adapters.                                            |
| `phoenix.tools`                                      | `phoenix_tools`          | Workshop/GM tool UI.                                                                          |
| `phoenix.files`                                      | `phoenix_files`          | Native/browser user file and clipboard services.                                              |

`SessionPort` has one declaration owner: `phoenix.session.api`. Application state imports that interface instead of redeclaring the class. Session factories belong to their implementations, so importing the shared interface does not offer authoritative factories. `HostAccess` is exported only by the server-side local-session module for use by host/fleet composition.

Content, transport and replication depend on the foundation rather than application state. Application depends on the shared session API. This keeps the import/link graph acyclic. CMake rejects simulation, Flecs, local-session, network-host and fleet-setup dependencies in shared frontend layers; client and Workshop executable guards remain in place. Simulation continues to exclude graphics and tools.

Blob storage depends only on the foundation; journal storage depends on replication. Neither needs the content/archive stack for hashing or file IO. Native and browser transports share a private C++ policy for discovery, signalling envelopes and route selection. Platform adapters retain socket and WebRTC mechanics. `DurableConsensus` owns journal recovery and write acknowledgements; host/fleet drivers retain their application-specific commit handling.

## Authoring module code

Public interfaces live beside their implementation in `.cppm` files and are registered using public `CXX_MODULES` file sets. `cmake/Modules.cmake` records the target ownership and enables scanning for first-party consumers; foundation interfaces are registered when their target is created. Keep dependencies represented in both imports and CMake target links.

Each exported declaration has exactly one module owner. Implementation `.cpp` files use `module phoenix.name;`; consumers use `import phoenix.name;`. Do not introduce parallel compatibility headers or attach another declaration of the same class to a different named module.

Standard-library and third-party includes precede the named-module declaration in a global module fragment:

```cpp
module;
#include <string>

export module phoenix.example;
export import phoenix.contracts;

export namespace phoenix
{
std::string describe(const BodyView &body);
}
```

Consumers include standard-library headers for the operations they use, then import Phoenix modules. Imports do not promise the old headers' incidental transitive includes. JSON stays the same `nlohmann::json` type through `Json` and `WireJson`; units using JSON templates may include its header in the global fragment. There is no header-unit conversion, `import std`, or custom BMI cache.

Remaining private helper headers are implementation fragments included after the module declaration/imports. Their standard-library/third-party dependencies belong in the including unit's global fragment. Shared inline constants and methods must be explicitly inline: named modules change the usual header linkage rules. Projection helpers shared by host and client have internal linkage.

Windows headers/macros and Emscripten headers remain local to their adapters. Browser callbacks retain their existing `extern "C"`, `EMSCRIPTEN_KEEPALIVE`, exported names and JavaScript bodies. No packet, save, content or storage schema changes accompany this conversion.

## Building and verification

The minimum CMake version is 3.28; bootstrap continues to pin 3.31.6, Ninja 1.11.1.3 and Emscripten 6.0.12. The native generator remains Visual Studio 2022. Use the normal [build and check commands](building.md). Emscripten's `emscan-deps` wrapper supplies module dependency scanning, including its target flags. First-party WASM consumers inherit consistent exception settings from the foundation.

Module interfaces are compiled for each toolchain/configuration in its own build tree. Do not copy native IFCs to WASM, or reuse BMIs across compiler/configuration changes. CMake orders interfaces before consumers and tracks changes to both interfaces and implementations. The formatter and existing source-root CI filters include `.cppm` files.

The automatic gate runs the existing synthetic behavioral tests and compares native/WASM continuation. It also executes input/semantics on WASM. Network-service tests exercise real native peers, client/GM sessions and verified WSS. Browser storage, real screen readers, devices and performance smoke journeys remain separately invoked evidence; compilation does not establish those requirements.

## Conversion evidence — 17 October 2026

The pre-conversion baseline and converted automatic gate passed: ten native integration cases, the WASM checks (including the newly added input/semantics case), native/WASM continuation comparison, and five network-service cases covering discovery/relay, plaintext-bind refusal, live fleet recovery, client/GM sessions and trusted/untrusted WSS.

Clean builds rebuilt all targets and dependencies successfully. The following local Release timings used eight build workers, MSVC 19.38 with the Visual Studio generator, and Emscripten 6.0.12/Clang 24 with Ninja. These are single observations, not a before/after performance comparison.

| Build experiment                                               | Native   | WASM     |
| -------------------------------------------------------------- | -------- | -------- |
| All targets, `--clean-first`                                   | 104.64 s | 109.90 s |
| All targets, no source changes                                 | 2.15 s   | 0.18 s   |
| Touch input implementation; build input/semantics target       | 1.90 s   | 3.56 s   |
| Touch contracts interface; build simulation integration target | 9.33 s   | 10.87 s  |

The implementation experiment rebuilt input and relinked its test without recompiling unrelated consumers. The interface experiment rebuilt contracts, the dependent simulation interface/implementation and its test. These were timestamp invalidation probes; no test-only API or behavior changes were retained. Native's all-target build also invokes the existing training-pack generation target, so its no-change result is not a pure compiler measurement.

Compiled server/client/Workshop export tables retain their semantic, file, transport and applicable storage callbacks. The minified storage/blob probes retain their JavaScript `Module` callback bindings for `ccall`. This checks linkage and exported names, not actual browser storage or accessibility operation.
