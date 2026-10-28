# Live GM inspection

`phoenix.workshop.gm` owns GmInspector selection, map camera, pointer captures and
texture cache. `gm.cppm` accepts only a SessionPort, visible status and ToolFrame.
It never copies Application or opens an editable WorkshopDocument.

Inspection comes from the session's existing permitted inspection projection.
The shell draws common preferences/navigation chrome; shared map navigation
supplies gestures and icons. Session changes reset transient inspection state.
Clear GPU resources before graphics context teardown.

Network client/GM and input/semantics integration retain their normal contracts.
An actual live inspection and embedded GM journey remain manual smoke evidence.

# Dock ownership

GM windows use distinct stable ImGui window/dock identities from authoring windows. Switching between GM and Workshop therefore retains separate docking, selection and camera state even when both use translated labels such as Map or Properties.
