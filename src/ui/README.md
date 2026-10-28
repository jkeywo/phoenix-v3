# Player UI family

Start with [interaction records](interaction/README.md) and the renderer-free [Console model](console-model/README.md). Then read [Console surface](console-surface/README.md), its [flight](flight-widgets/README.md), [tactical](tactical-widgets/README.md), [engineering](engineering-widgets/README.md), [operations](operations-widgets/README.md) and [information](information-widgets/README.md) collaborators.

[Runtime](runtime/README.md) composes those surfaces with [menus/settings](screens/README.md) and the [Viewscreen](viewscreen/README.md). [Preview](preview/README.md) owns the isolated authoring sample. Native/browser adapters share all these C++ implementations. Rendering and accessibility consume the same permitted control descriptions; only SessionPort grants command admission.
