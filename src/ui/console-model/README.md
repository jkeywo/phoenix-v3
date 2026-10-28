# Permitted Console model

Start with [console.cppm](console.cppm).

ConsoleModel owns one route observation, browsing and pending desired values. It invalidates on owner/generation/content/claim changes and submits normal SessionPort commands.

Dependencies: Shared session API, eligibility contracts and portable localisation; no renderer, devices or authoritative World.

Validation: Existing input/semantics, session, controller-runtime and domain synthetic cases.
