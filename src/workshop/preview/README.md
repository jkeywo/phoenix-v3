# Authoring graphics previews

`phoenix.workshop.asset_preview` owns draft/dependency asset lookup, skybox
compatibility caching and GPU textures. Map and appearance editing use the same
lookup rules through `assets.cppm`.

`phoenix.workshop.material_preview` owns scene renderer, render texture, camera,
motion sample, dust configuration and resolved preview catalogue. `material.cppm`
accepts only the document, selected definition index and feedback. The shared
runtime renderer consumes the resulting authored sample scene; this preview never
acquires gameplay authority.

Clear resources after draft asset replacement and before graphics context teardown.
Editor camera/sample settings are transient. Production asset/content verification
remains automatic; visual material calibration and preview operation are manual smoke.
