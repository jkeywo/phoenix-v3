# Remaining working-contract fixes

Implementation follow-up, 2–3 November 2026. This record covers the four findings from the second AGENTS.md review. It does not extend the earlier cold-start measurement receipt to these source changes.

## Ownership and behavior

- Knowledge records provide an immutable lookup for the latest direct shared stream, keyed by entity, schema, subject and source. Encounter objectives compare typed fields against that record, including withdrawals; the bounded public presentation board no longer supplies publication state. Load-time bindings retain numeric schema, producer and field IDs. Camera revision calculation uses the same typed lookup. Existing records supply continuation, without a new save format.
- Dialogue reads finding tags decoded at Knowledge admission or restore. Tag-only findings remain useful for dialogue independently of detailed scanner validity. The permitted projection, latest-stream suppression, freshness, withdrawal and addressed-reader checks remain in force. Ordinary dialogue decisions no longer parse finding JSON or resolve schema fields.
- Prepared content retains compiled widget readout accessors. Compilation validates command, field, role and fitted source-kind compatibility; optional permitted-board paths retain their existing semantics. Console refresh uses compiled source/field/pointer descriptors. Both lightweight Workshop document previews and preview-session replacement carry the compiled cache. Authoring/export and presentation wire formats remain unchanged.
- The explicit Controller/Workshop graphical diagnostic uses schema-2 shared widgets. It edits the reusable widget through the shared authoring backend, verifies and exports/reopens the draft, then runs a disposable host. Its fixture's AI officer reports permitted requested speed and nonzero observed velocity; the probe checks that evidence separately from command consumption. Pause, Step and Restart remain part of the journey. The dependent two-Console GUI fixture uses the same current format.

The changes preserve the packaged startup verification/preparation handles, authored production Console layouts, workloads and tuning. The documented logistics assistance and navigation lookout exception remain unchanged.

## Validation

The following checks ran against the combined follow-up source. The earlier receipts retain their original scope.

| Check                                 | Result                                                                                                              |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| Module ownership                      | Passed: 770 C++ sources, 245 module units, zero violations                                                          |
| Shared Workshop authoring             | Passed: 517 field descriptions                                                                                      |
| Launcher/trace/deployment JavaScript  | Passed: 48 cases                                                                                                    |
| Native short profile and build check  | Passed: 19 registered short checks and the production executable build check                                        |
| Affected native domain checks         | Passed: Comms and encounter publication, flight and weapon objectives                                               |
| Maintained content verification       | Passed: base, test missions, test and test-3d                                                                       |
| Synthetic fixture verification        | Passed: shared, split and two-Console packs; CLI edit, export and reopen                                            |
| Affected WASM domains                 | Passed: Knowledge, encounter publication, Comms and Controller runtime in both ordinary and threaded Release builds |
| Explicit native graphical diagnostics | Passed: Workshop Test/Pause/Step/Restart, shared/split Controller reports, and two-Console navigation               |

Synthetic regressions cover objective omission beyond 128 presentation records, typed source/entity isolation, withdrawal and restored revisions; admitted dialogue tag replacements, malformed/latest evidence, scope and expiry; invalid widget fields/roles/source kinds, multiple fittings, optional/escaped board paths, content reload and Workshop preview caches.

The graphical probes wait for rendered controls and committed observations. They independently exercise star allocations, Away/resume and AI officer Message History. The public Viewscreen capture contains the emergency report and excludes the addressed private report. The Workshop capture shows the disposable host restarted from its exported snapshot. Captures and logs are under `build/compliance-followup-graphics/final/`; these are synthetic graphical journeys, not physical device or screen-reader evidence.

This follow-up used the short native profile and focused native/WASM checks. The full long suite, live service matrix and full native/WASM continuation matrix were not rerun. Browser rendering, physical screen readers/devices, six-display operation and performance evidence remain separate acceptance requirements.

## Reproduction and source records

Use the ordinary [short validation commands](testing.md#execution-and-maintenance-rules) and the affected Knowledge, Controller, conversations and interdiction targets. Graphical fixture generation and invocation are documented in [Controller authoring](controller-content.md#compatibility-and-verification).

Implementation commits: `4f0c75d6` (typed publication), `cfdb2889` (dialogue evidence), `86c39362` (widget bindings) and `758825a1` (graphical probes). Affected WASM results are in `build/compliance-followup-ordinary-domains.log` and `build/compliance-followup-threaded-domains.log`.

Affected module reading guides describe the new ownership. The generated module inventory and graph use normalized C++ source fingerprint `28c06b6d203f0938e863cade918a4632525df0999e68d304999937c464308e77`; their counts describe source composition, not gameplay acceptance.
