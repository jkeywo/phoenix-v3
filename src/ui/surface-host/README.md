# Console document services and choice descriptions

`host.cppm` (`phoenix.ui.surface_host`) owns one document's element/property/ObserverPtr markup caches and generic control placement. The Console surface owns the Rml context/document lifetime and binds it once; panels borrow this concrete service synchronously. Explicit invalidation calls preserve mounted DOM/focus and the original refresh order.

`ConsolePanelOutput` publishes controls, readouts, inputs, toggles and measured placements through borrowed destinations. It imports input/interaction contracts and has no Application or session capability. Domain panels receive a permitted ConsoleModel plus the small frame facts they need.

`choices.cppm` (`phoenix.ui.choice_specs`) is a separate renderer-free owner. Camera mode/direction differ by typed descriptions: labels, icons, artwork, aspect and option geometry. One renderer constructs, measures and updates both. ConsoleModel and authoritative admission still own selection/commands; the descriptions contain no scripts or authority rules.
