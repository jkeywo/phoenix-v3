# Personnel and operation definitions

Read compiler.cppm and types.cppm first.

Officers, teams, clinical policy, away slots and boarding encounter definitions.

Dependencies: Catalogue, state-tree and effects contracts plus explicit physical compiler collaborators.

Ownership: Compiled definitions never acquire current team, patient or party progress.

`personnel.cpp` compiles optional team `local_workflow` bindings in the separate
`team_workflow` context. Its registry exposes admitted assignment scope, arrival,
active-order/completion facts and declared local eligibility/start actions. It
resolves only the fitted team's patient orders with Medical capability; Controller
commands and world queries are unavailable. Shared Workshop vocabulary uses this
same registry. See [clinical workflow contracts](../../../docs/agent-compliance.md).

Validation: Personnel, medical, away and boarding production-loader cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
