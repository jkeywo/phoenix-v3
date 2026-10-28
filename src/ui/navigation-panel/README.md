# Navigation and mission panel

`phoenix.ui.navigation_panel` owns local navigation camera, selection, pointer/pinch/cancel state, keyboard/wheel projection changes, navigation/mission-report markup and drawing. Read `navigation.cppm` for the interface, then `navigation.cpp` for preparation, input and rendering in order.

The surface routes events and publishes a bounded frame. The panel consumes a permitted ConsoleModel; waypoint actions use the existing model path. Asset/share-markup functions are synchronous presentation capabilities, and return-home is supplied explicitly. No Application pointer, World or arbitrary state lookup enters the owner.

Browsing snapshots contain only camera and selected coordinates, matching the previous surface behavior. Input cancellation retires captured contacts, and pointer results tell the host when selection invalidates its render. DOM/context lifetime belongs to the surface; texture cache/catalogue references are borrowed only during drawing.
