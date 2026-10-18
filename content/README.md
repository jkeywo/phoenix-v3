# Base game and training mod

`base/` supplies Phoenix Base Game (`phoenix-base`): Courier, reusable scenery, bridge Console and sprites, plus Free Flight. Native builds package it as `build/native/base.zip` and enable it in a fresh library. Existing `training.*` IDs on shared templates remain stable for authored-map compatibility.

`training/` supplies Phoenix Training (`phoenix-training`), a separate mod that depends on `phoenix-base`. It contains navigation and lighting scenarios, practice ship variants and patrol AI. Builds verify and package it against `base.zip` as `build/native/training.zip`; it is installed disabled on fresh native and browser libraries. Enable it in Mod Packs to apply it over the base game. Every pack, including the base, can be disabled. Enabling a mod enables its dependencies; disabling a dependency disables dependent mods.

These authored gameplay/prototype packs are used in explicitly invoked smoke journeys. Automatic system integration fixtures remain independent under `tests/fixtures/`.

Eight-way sprite entries are ordered by camera bearing relative to ship heading: 0, 45, 90, 135, 180, 225, 270 and 315 degrees. Heading 0 points along world +X (east); 90 points along +Y (north). Camera-facing billboards use one image. Render scale is billboard height in metres, with source image aspect preserved.

To package other source folders, run `build/native/Release/phoenix_pack.exe source-directory output.zip [ordered-dependency.zip ...]`. The output is written only after production loading, dependency resolution and verification succeed. Workshop ZIP export remains the normal player authoring path.
