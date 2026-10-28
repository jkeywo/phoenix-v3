# Tool shell and controls

`phoenix.workshop.shell` owns ImGui font/theme/backend resources, common controls,
translated `ToolMessage` feedback and presentation preferences. `ToolFrame`
contains only region, keyboard eligibility, pointer samples and preferences;
`draw_tool_frame` invokes the caller's composition callback. It owns no document,
Application or simulation. `phoenix.workshop.authoring` adds typed JSON field
controls used by domain and tree editors.

JSON controls are compiled in `fields.cpp` and `authoring.cpp`. Scalar controls expose string, integer, boolean and decimal overloads; array/object inspectors take ordered callbacks. Consumers do not instantiate exported JSON control templates. Each field is written only after its control reports a change, and row removal follows the original iteration order.

`phoenix.tools` in `tools.cppm` is the application composition interface. Its two
composition sources connect the shell to a WorkshopWorkspace or GmInspector.
The browser test adapter retains its existing public callback names.

Dependencies are content, portable localisation, graphics/platform services and
ImGui/raylib. Font/texture resources belong to the graphics thread; teardown
closes workspaces before destroying the backend. Verify shared semantics and
menu contracts with existing system cases; tool skin/navigation is manual smoke.
