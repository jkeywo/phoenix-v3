# Command-line content verification

`verify.cpp` loads an ordered list of archives and calls production resolve_packs. It reports pack identity plus verified catalogue counts and returns failure on invalid input. Archive parsing and cross-domain checks remain in their content owners.

The dependency points toward packs and compiler/verification targets, with no World, renderer or tool document lifetime. Use this entry point to verify authored data; it is not a gameplay system journey or a balance/performance assertion.

Existing content tests cover loader refusal and dependency/override behavior. Verification of a particular pack is separate evidence from native/browser builds and runtime continuation; record actual commands/results in migration receipts.

Pass `--inventory output.json` before the ordered archive paths to export
resolved entity definitions after verification. This includes fitted assembly
and equipment expansion and is useful for authoring migrations and audits.
The export is local diagnostic data; it does not instantiate a World or change
the archives.
