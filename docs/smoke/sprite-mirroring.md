# Sprite mirroring validation — 20 October 2026

Optional per-frame `mirror_x` is implemented in source sidecars, canonical materials, Workshop and the shared native/browser renderer. See the [authoring contract and eight-bearing example](../content-format.md#sprite-region-mirroring).

## Automated checks

The native `system.content-and-packs` case and WASM `phoenix_content_check.js` passed. The independent 20×4 synthetic `mirror-atlas.png` contains five 4×4 regions, with horizontal colour asymmetry in each. Tests import eight bearing entries sharing those five regions, retain source pivots and markers, export/reload compiled content through Workshop, and verify permitted crew transfer. They also cover named entries, standalone single-image sidecars, legacy defaults, invalid string/number/null flags and transactional refusal.

Native and all three WASM applications built. The native `--build-check` passed. The production pack utility generated the shipped base and training packs, and `phoenix_verify` accepted their combined 12 entities, 8 maps and 8 Consoles. These are structural content checks, not balance or device acceptance.

## Explicit graphical evidence

The manually invoked `phoenix_comic_probe` passed on Windows OpenGL and in the in-app browser using WebGL 2. `tests/manual/mirroring.hpp` adds synthetic asymmetric albedo, normal and depth maps, an off-centre pivot and an engine attachment. Both ordinary and instanced paths capture original/mirrored albedo, depth, normal and shadow views. The ordinary path also captures engine placement. The mirrored shadow is compared with the horizontally reflected source shadow under symmetric sunlight, with the receiver border excluded from the check.

Captures use `mirror-{ordinary,instance}-{source,flipped}-{albedo,depth,normals,shadow}.png`, plus ordinary engine captures. Native output is in `build/native/Release`; the browser probe exports its captures to its virtual filesystem. Representative native engine, instanced normal and instanced shadow captures were visually inspected; the browser final shadow view and successful assertions were also inspected. No physical-display, input-device or screen-reader check is claimed.

Validation ran in the attached `sprite-mirroring` worktree because another chat rebased the primary checkout during implementation. Logs are under its ignored `build/mirror-*.log`; the browser screenshot is `build/mirror-webgl.png` in the primary checkout. The initial base revision contained a truncated native polling function and committed Console merge markers. Temporary build repairs matched the subsequently completed main-branch integration; they are excluded from the mirroring commit.
