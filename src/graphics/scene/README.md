# Shared authorised scene renderer

Start with [scene.cppm](scene.cppm).

SceneRenderer owns private sprite/model backends, shaders, instance buffers and environment resources. Clear before content replacement and destroy before closing the graphics context.

Dependencies: Content definitions, graphics/assets and graphics/geometry. Callers supply permitted SceneFrame records.

Validation: Native/browser builds and existing manually triggered comic/material/view smoke.
