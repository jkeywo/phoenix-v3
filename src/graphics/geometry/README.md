# Pure presentation geometry

Start with [geometry.cppm](geometry.cppm).

NavigationCamera owns only local camera position/scale; sprite-facing helpers are stateless. `ShipCameraPose` and the out-of-line `ship_camera_pose` in `ship_camera.cpp` own ship-anchored first/third-person geometry. Viewscreen and the synthetic camera case import this same compiled implementation. No GPU, session or Application is required.

Dependencies: Portable standard-library math; foundation carries consistent compiler options.

Validation: Existing controller-runtime camera and menu sprite-facing integration cases.
