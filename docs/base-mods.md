# Launch bases and development content

The application selects a fixed base at startup. Native uses `--base <pack-file>`; the browser launcher uses `?base=<pack-url>`, resolved relative to the launcher page. Ordinary launches use Phoenix Base Game. Explicit base load failures do not select another game.

Root manifests declare `"base": {"render_mode":"sprites","interface_style":"current"}`. Rendering supports `sprites` and `models`; the current interface is the initial supported style. Children declare one `parent` pack ID. Their ancestor chain must reach the selected base. Multiple branches can be enabled with parents before children and explicit overrides. A running application cannot remove, disable or replace its base.

Player name/avatar, pack selections and saves are isolated by local profile and base ID. Content hashes pin session/save compatibility; updating a base retains its preference namespace. Language, accessibility and audio remain local-profile preferences. Archive blobs are shared by hash and are retained when another base might reference them. Existing preferences migrate into the default Phoenix base namespace.

Invitation URLs include the base reference. Native hosts using a custom local archive can pass `--base-url <browser-accessible-pack-url>` for joining devices. The archive must be served to those devices; a native filesystem path is not a download URL. Admission checks the launched base ID and hash before accepting participants. Saves require their original base revision.

## Development roots

`content/test` replaces Training. It is an independent sprite base with `test.*` definitions. `content/test-3d` is an independent model base using `test3d.*` definitions and copied V2 static assets, with provenance in `sources.json`. Neither depends on production content at runtime.

Build explicitly:

```powershell
./scripts/build.ps1 -Target native -BuildTarget development_packs
./build/native/Release/native.exe --base build/native/test.zip --local-only
./build/native/Release/native.exe --base build/native/test-3d.zip --local-only
```

For browser development, serve the web output and put the chosen archive beside it, then open `?base=test.zip` or `?base=test-3d.zip`. These roots are available for agent feature work and explicitly invoked automated smoke journeys. Automatic behavioral assertions continue using independent synthetic fixtures through production paths, as required by the testing policy.

Development archives are explicit targets, uploaded to a separate CI evidence artifact. Public deployment stages production files from an allowlist and checks that development packs/assets are absent. The browser does not discover test roots automatically.

## Model presentation

The launch base selects a sprite or model implementation behind `SceneRenderer`. Model entities use positive `render.scale`, independent radar presentation, and exactly one of `render.model` or `render.star`. They do not need sprite material fields. All definitions in a rooted catalogue must match its rendering family, including future spawn definitions.

A model declares an `asset` pack address, optional XYZ `offset`, degree-valued XYZ `rotation`, and positive XYZ `scale`. Embedded static GLB triangle meshes support node transforms, base colour and emissive materials. External resources, animations, skins and required unsupported extensions are rejected before GPU upload. Normal-map and metallic/roughness shading are deferred.

Named `render.attachments` use model-local `position` and nonzero `direction` XYZ vectors. Engine-prefixed markers drive presentation-only exhaust and lighting from actual thrust. The renderer exposes world-space attachment position/direction for both visual families. Simulation geometry and weapon origins stay independent of camera-selected sprite frames.

A procedural `star` accepts surface/hot/cell/halo colours, animation speed and halo radius multiplier. Its sphere surface and corona adapt V2's shaders to native GLSL and WebGL 2. Models use ambient/directional illumination, depth-tested shadows and bounded engine lights. The GUI, radar and simulation retain their common implementations.

Maps assign `presentation.skybox` to a pack-qualified panorama or vertical cube-strip PNG. Both renderers use the same translation-free sky background, camera projection and navigation effects. The model test map assigns V2's cube strip, baked per face to 512 pixels so compiled exports remain within archive bounds. `scripts/prepare-v2-skybox.py` reproduces that derivative; `sources.json` records both hashes. The flyable Courier authors `render.reference_grid`; map dust and local reduced-motion/grid/dust preferences use the existing shared effects.
