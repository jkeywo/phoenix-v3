# State-tree authoring

`phoenix.workshop.tree` owns selected tree/state, context vocabulary and a copy of
the permitted disposable-test feedback used for graph highlighting. Its small
TreeEditor interface in `tree.cppm` exposes tree drawing and typed binding editing.

Compilation and context registries come from production content. Generic field
controls come from workshop authoring controls. Native execution never runs in
this module; editing, inherited overrides and runtime highlighting remain distinct.
The state is transient and requires no continuation migration.

Existing state-tree/content and Workshop-host cases protect compiled definitions
and execution. Interactive graph editing/highlighting remains manual smoke.
