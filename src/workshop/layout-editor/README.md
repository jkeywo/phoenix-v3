# Console layout editor

`phoenix.workshop.layout` owns LayoutEditor state behind `layout.cppm`. It takes
an editable WorkshopDocument, Console index, pseudo-localisation flag, file-import
inbox and feedback. It does not depend on Application or gameplay authority.

The implementation resolves production UI definitions and uses `phoenix.ui.preview`
for an isolated sample surface. `LayoutPreviewSettings` exposes resolution,
sample state, text scale, pages and header choice, retaining the existing manual
smoke controls. Drag/drop, rename and focus state stay private to the editor.

Dependency definitions may be previewed without becoming editable or verified.
There is no persisted editor continuation. Existing content/UI binding cases
protect authoring contracts; layout interaction/responsive preview is manual smoke.
