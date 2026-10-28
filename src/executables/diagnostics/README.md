# Build checks and manual frame profiles

`build_check.cpp` is the explicit headless build/continuation diagnostic entry point. `native_profile.cppm` exports `phoenix.diagnostics.native_profile` (target `phoenix_native_frame_profile`): FrameCapture owns native sample collection and writes the report/framebuffer through its out-of-line record method.

`browser_profile.cppm` exports `phoenix.diagnostics.browser_profile` (target `phoenix_browser_frame_profile`). BrowserFrameProfile keeps URL/report JavaScript private in `browser_profile.cpp`. The browser owner depends on application contracts and raylib only; a threaded server may supply a background-profile callback, evaluated only when reporting. Native profiling may link the local session adapter.

These are measurements of existing execution, not simulation policy or mandatory timing tests. Preserve warmup, sample windows, units and reporting order. Newly extracted diagnostic owners require their own compile/link checkpoints; actual captures and platform/runtime results belong in migration evidence.
