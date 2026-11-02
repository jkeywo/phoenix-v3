# Shared Controller action execution

`phoenix.control.execution` owns action selection/running/cancellation, staged command admission, report receipt mutation and full Controller checkpoint serialization. `execution.cppm` exposes borrowed typed `DecisionObservation` and output/command ports. No unrestricted World, Flecs or networking context enters the interpreter.

`execution.cpp` preserves tree evaluation, transactional staging, operator generations and interruption cleanup. `admission.cpp` owns bounds, idempotency, request identity and revision admission with explicit physical-validity callbacks. `report_receipts.cpp` records successful sends using canonical Controller report identities. `persistence.cpp` preserves the current saved representation and validates native arguments and known peers.

Observation preparation, domain candidates and objective arbitration now belong to `../decisions`; generic integer scoring has its own `phoenix_control_objectives` target. Import those owners directly rather than relying on execution re-exports. World continues to route all human/AI proposals through authoritative admission.

Existing Controller runtime/handover/continuation integrations protect this seam. Verification and remaining platform limits are recorded in `docs/module-ownership-followup.md`.

Staged command observation reads and action destination/contact inputs use numeric slots compiled with the Controller template. Action execution and its private references share that registry; evaluation never interprets command observation names.
