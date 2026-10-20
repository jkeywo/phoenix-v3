# Production and development base packs

`base/` supplies Phoenix Base Game (`phoenix-base`): Courier, reusable scenery, bridge Consoles and sprites, plus Free Flight. Ordinary builds package it as `build/native/base.zip`. Launch selects and locks this base by default.

`test/` is the standalone development sprite base (`test`), with navigation and lighting scenarios, practice ship variants and patrol AI. `test-3d/` is the standalone model base, with V2 Courier/asteroid assets, an animated procedural star and the V2 skybox. Both own their complete content closure and separate settings namespaces. Neither depends on the production base. Build them deliberately using `development_packs`, then select one with native `--base build/native/test.zip` / `--base build/native/test-3d.zip` or browser `?base=test.zip` / `?base=test-3d.zip`. They are excluded from deployed runtime archives and published menus.

Children declare a single `parent`; enabled descendants enable ancestors and disabled parents disable descendants. Multiple branches may be enabled with parents first. The launch base cannot be removed, disabled or replaced while running. See [launch-base contract](../docs/base-mods.md).

These authored development packs support feature exploration and explicitly invoked smoke journeys. Automatic system integration fixtures remain independent under `tests/fixtures/`.

Eight-way sprite entries are ordered by camera bearing relative to ship heading: 0, 45, 90, 135, 180, 225, 270 and 315 degrees. Heading 0 points along world +X (east); 90 points along +Y (north). Camera-facing billboards use one image. Render scale is billboard height in metres, with source image aspect preserved.

To package other source folders, run `build/native/Release/phoenix_pack.exe source-directory output.zip [ordered-dependency.zip ...]`. The output is written only after production loading, dependency resolution and verification succeed. Workshop ZIP export remains the normal player authoring path.
