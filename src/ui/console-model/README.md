# Permitted Console model

Start with [console.cppm](console.cppm).

ConsoleModel owns one route observation, browsing and pending desired values. It invalidates on owner/generation/content/claim changes and submits normal SessionPort commands.

Dependencies: Shared session API, eligibility contracts and portable localisation; no renderer, devices or authoritative World.

Shared widget readouts use the prepared catalogue's compiled accessors. Refresh reads only the permitted Observation and projected Controller equipment bindings; it does not parse authored paths, infer equipment roles or compile missing accessors on demand.

Availability checks borrow the immutable observation. Widget construction indexes control rows by projected owner for the duration of a single build and retains their original order within each owner.

A refresh can reuse the current model when the route still presents the same immutable snapshot and the selected Console is unchanged. Tab changes and expired optimistic requests still run reconciliation. The ordinary SessionPort remains responsible for command admission.

Tactical and support presentation reuse the model's ordered authored Console specifications. Each auxiliary still needs its own granted observation before its controls or readouts can be used.

Validation: Existing input/semantics, session, controller-runtime and domain synthetic cases.

Asset assignment and withdrawal resolve the owning Controller and native operation from the permitted Console observation. Shared widget parts filter that resolved operation and retain its generation for activation.
