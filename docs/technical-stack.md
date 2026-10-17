# Phoenix V3 technical stack

Status: selected direction, 17 October 2026, with the build foundation in progress. These choices replace the earlier Bevy/HTML/Ultralight implementation directions. The [GDD](GDD.md) defines gameplay; the [testing policy](testing.md) defines how implementation and authored data are checked. See the [implementation record](implementation-plan.md) for actual completion and remaining proof obligations.

## Selected components

| Responsibility | Selection | Boundary |
|---|---|---|
| Application and simulation | C++20 | Keep authoritative gameplay independent of graphics, UI and browser APIs. |
| Entities, components and execution infrastructure | Flecs | Own simulation entities, relationships, queries and explicit processing phases. Library scheduling does not substitute for resource-allocation policy or stable gameplay ordering. |
| Rendering, audio and ordinary platform facilities | raylib | Viewscreens and custom graphics, with explicit adapters where the platform backend does not meet input/media needs. |
| Player console UI | RmlUi | Styled controls, responsive layout and text; compile for native and WebAssembly. RML/RCSS are UI assets, not a browser application or embedded browser engine. |
| Radar presentation | Custom GPU-rendered widgets | Batched contacts, uncertainty, tracks and overlays. RmlUi supplies layout/surrounding controls; a shared permitted model supplies visual and accessible representations. |
| Workshop and GM UI | Dear ImGui docking | Docked panels, menus, lists and inspectors. Basic accessibility is required; screen-reader support is not part of the initial tools requirement. Localisation remains required and must be verified. |
| Web compilation | Emscripten/WebAssembly | Compile the C++ application/UI; keep handwritten HTML/JS restricted to a common host shell and browser services. |
| Text and localisation | RmlUi shaping-capable font integration using HarfBuzz/FreeType; shared translation resources | Verify actual font fallback, shaping, direction, input/composition and text expansion. Do not assume raylib text drawing provides the same shaping automatically. |

Clay is not part of the selected baseline. There is no separate HTML/JS application frontend, Ultralight dependency or Bevy runtime. Dear ImGui and RmlUi read the same domain projections and submit normal authorised commands; they do not own parallel gameplay models.

The initial build uses CMake and static libraries with commit/hash-pinned dependencies; see [building](building.md). Networking libraries, file encodings and public APIs are selected as their increments are implemented. Do not infer those choices from V2 or assume that a raylib/RmlUi/ImGui adapter already meets all required behavior.

## Runtime boundaries

Run one simulation per participating ship server, independently of that server's local screen count. Each ship server maintains the replicated deterministic full world and advances committed peer-consensus inputs/tick frames; the [implementation plan](implementation-plan.md) specifies roster, quorum and recovery rules. A client process can show several Console projections while retaining each Console's control and knowledge boundaries. Remote phone clients need their permitted state and local UI, not an authoritative copy of every hidden world object. Replication is not implemented yet.

The native/browser adapters translate platform events, services and asynchronous outcomes into supported application inputs. Keep gameplay rules, screens, widgets, state trees and job logic in C++ and authored data. Renderers consume presentation projections rather than unrestricted simulation internals. Headless system integration tests use the same content loader, admission and execution mechanisms without opening graphics windows.

The browser adapter covers networking/signalling, persistent storage, file import/export/drop, text entry and IME, clipboard, focus/accessibility, media permissions/capture and application lifecycle. Emscripten and raylib may supply parts of this glue. Minimal handwritten JavaScript does not mean no generated/runtime JavaScript. Browser storage must explicitly persist; ordinary in-memory filesystem writes are not saves across reloads.

A browser-authoritative host is not an ordinary listening LAN HTTP/WebSocket server. The transport design must supply appropriate signalling, peer connectivity and relay/service behavior. Preserve the GDD's installation-free browser route rather than silently replacing it with a native-host prerequisite.

## Native six-screen target

Support **six 1920 × 1080 displays at 60 Hz**, normally one Viewscreen plus five player consoles. **One borderless window spanning the configured desktop is acceptable.** Represent monitors/console regions explicitly; support host configuration of their placement rather than assuming all six monitors form one horizontal row.

Target one complete presentation every approximately 16.7 ms on a declared reference machine. This is a target, not a measured result or an automatic assertion on arbitrary CI hardware. The reference machine remains to be selected and recorded with the first performance capture. The six active display regions total approximately 12.4 million pixels; desktop gaps and the chosen spanning framebuffer can add work. Check actual framebuffer dimensions and graphics limits during setup.

Run simulation once; draw one 3D Viewscreen and five primarily 2D consoles. Maintain independent UI contexts and per-console state while sharing suitable assets, compiled geometry and font resources. Cache unchanged layout/text, batch radar geometry, bound histories and virtualise large lists. Keep animation/rendering smooth independently of simulation tick rate. Changes in evidence still follow observation rules rather than rendering frequency.

Measure simulation, projection, UI layout/text, radar geometry, GPU and presentation costs separately, including frame-time spikes. Do not assume that fewer libraries, an extra thread or six separate processes guarantees speed. Multiple independent native windows are not required by the selected baseline, avoiding reliance on unsupported stock-raylib multi-window behavior.

## Console-specific input

Map device input to stable Console identity, separately from monitor location, OS keyboard focus and gameplay command authority.

- Gamepads have explicit console assignments, dead zones and per-console navigation/interaction focus. One player's navigation must not move another player's selection.
- Touch uses genuine pointer/contact events. Route each contact into its console region, converting desktop/window coordinates into local coordinates; retain capture through the interaction. Simultaneous contacts on different consoles must not collapse into a shared emulated mouse.
- Keyboard/text entry and screen-reader navigation have an explicitly selected console. Do not let another player's touch/gamepad silently redirect text input or accessibility focus.
- Console movement/reconstruction retains logical identity and device assignment. Disconnect or reassignment clears held input so thrust, firing, dragging and activation do not stick. Reconnection requires safe recognition/assignment rather than trusting a transient numeric index.
- Validate actual device capacity and backend behavior. raylib's default gamepad capacity is four, and an XInput-only route has a four-controller limit. Supporting five console gamepads requires more than increasing an array limit.

Native touch, input-method and controller integrations are proof obligations for the platform layer; do not claim complete support from API availability alone. Rendering/input assignment never grants command authority to an unassigned operator.

## Accessibility and localisation

**Player consoles require screen-reader support**, alongside keyboard/controller navigation, text scaling, contrast, non-colour-only status, reduced-motion support and visual/text equivalents for important sound. Workshop/GM tools require basic navigation/scaling/contrast and feedback accessibility; initial screen-reader support is not required there.

Each player widget exposes stable semantic identity, role, name, value, state and supported actions from the same permitted model as its graphics. Browser builds expose an appropriate DOM/ARIA representation; native builds expose an OS accessibility provider. The particular native bridge/library is not selected. RmlUi's internal document tree does not automatically become a platform accessibility tree.

Radar contacts also have an accessible list/details representation with equivalent selection and actions, known position/uncertainty and provenance. Do not reveal hidden facts through accessibility or announce every animation frame. Meaningful announcements preserve the GDD's knowledge, alert and report rules rather than creating an omniscient danger feed.

Use shared translation keys/resources for player UI, authored content and tools. Test expanded strings, Unicode, glyph fallback, complex shaping, text direction, numeric formatting and IME where applicable to the declared language set. The launch language set is not selected yet. Dear ImGui's complex-script and accessibility limitations must not be mistaken for a localisation waiver: if required tool languages cannot be supported by the selected integration, resolve that explicitly before claiming support or expanding tool implementation.

## First integration proof

Before broad gameplay implementation, demonstrate the selected libraries working together: six representative display regions, custom populated radars, concurrent console inputs, a translated player control and a usable browser/native semantic action. Also exercise a browser phone client's authorised command and save/resume of running state-tree work.

Use synthetic data for repeatable behavioral assertions. Record actual six-screen/native-device/browser/screen-reader/performance runs as manually invoked smoke evidence. Successful compilation or a headless test is not evidence of physical touchscreen routing, screen-reader usability or the 60 Hz target.

## Source references

These references support library capabilities and constraints; the choices above come from the V3 stack discussion.

- [Flecs](https://github.com/SanderMertens/flecs), [raylib](https://github.com/raysan5/raylib), [raylib configuration](https://github.com/raysan5/raylib/blob/master/src/config.h).
- [RmlUi localisation](https://mikke89.github.io/RmlUiDoc/pages/localisation.html), [contexts](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/contexts.html), [render interface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html) and [shaping/input changes](https://github.com/mikke89/RmlUi/blob/master/changelog.md).
- [Dear ImGui](https://github.com/ocornut/imgui) and [docking](https://github.com/ocornut/imgui/wiki/Docking).
- [Emscripten networking](https://emscripten.org/docs/porting/networking.html) and [filesystem](https://emscripten.org/docs/api_reference/Filesystem-API.html).
- [Windows pointer input](https://learn.microsoft.com/en-us/windows/win32/inputmsg/functions), [XInput limits](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput) and [browser accessibility semantics](https://developer.mozilla.org/en-US/docs/Web/Accessibility/Guides/Accessible_web_applications_and_widgets).
