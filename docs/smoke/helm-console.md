# Native Helm and immediate Console placement

Date: 18–19 October 2026.

The Console action guard previously required the application-wide Console screen. A native host keeps the Viewscreen active while rendering local Console contexts, so this guard rejected the controls on its other monitors. Local input now accepts that presentation state while retaining session/generation and per-Console command checks. Absolute slider/ring requests use the same production Console model and command admission as controller step actions.

Assigning a native monitor now applies the layout immediately. The assigned Console renders its authored layout and a name/description card with local Ready/Back actions. Back clears the reservation and removes that display assignment; Ready participates in the existing countdown. Preview controls cannot advance the stopped simulation.

## Verification

The synthetic production-loader session test exercises absolute speed fractions, fractional headings, out-of-range refusal, independent System authority and committed requests. The complete automatic gate passed native integration cases, content verification, WASM checks, native/WASM continuation comparison and the real-network journeys (`build/helm-final-gate.log`). The final native and web application builds are recorded in `build/helm-final-native.log` and `build/helm-final-web.log`.

## Explicit graphical evidence

The available validation desktop reports one physical monitor. The explicitly labelled region smoke therefore supplies two test regions in one 1600 x 900 native window: an 800 x 900 host region and an independently routed 800 x 900 Console region. It uses the ordinary production lobby assignment/ready commands, actual RmlUi contexts, native UIA metadata and the production session/renderer. It does not claim physical two-monitor, multi-touch, controller, reader or six-screen performance acceptance.

```powershell
./build/native/Release/native.exe --local-only --game-pack build/native/base.zip --data build/helm-routing-final --lobby-regions-smoke
```

The journey checks immediate Console appearance before launch, name/description accessibility bounds, Back/removal/reassignment, the Console's own Ready action, countdown and running state. It then clicks the RmlUi vertical slider through its context, selects north on the heading annulus and requires the requests to commit on that Console route while the host remains on Viewscreen. Settings, managed saving and explicit quit remain part of the journey. Logs: `build/helm-completed-verified.log` (final) and the earlier `build/helm-regions-verified.log`. Captures: `build/menu-native-assigned-console.png` and `build/menu-native-helm-active.png`. These were visually inspected; they are manual evidence, not production-content goldens.

On equipment with multiple detected monitors, use `--lobby-displays-smoke` to exercise the actual monitor placement path instead. The one-monitor attempt was stopped with an explicit diagnostic rather than treated as physical evidence.

## Artwork

`content/ui/helm/` contains unchanged copies of the original throttle track/grip, radar bezel and background PNGs with equivalent sidecars from `raw/`, plus two transparent built-in image_gen assets: the readout housing and heading pointer. Exact generation prompts are in `content/ui/helm/generation.json`. Runtime labels/values remain live, localisable text. The shared Workshop Console preview uses the same controls and art.
