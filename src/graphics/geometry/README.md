# Pure presentation geometry

Start with [geometry.cppm](geometry.cppm).

NavigationCamera owns only local camera position/scale; sprite-facing helpers are stateless. No GPU, session or Application is required.

Dependencies: Portable standard-library math; foundation carries consistent compiler options.

Validation: Existing controller-runtime camera and menu sprite-facing integration cases.
