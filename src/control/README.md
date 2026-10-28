# Console control

Read `controllers/controllers.cppm` for Controller-owned state, `ownership/ownership.cppm` for human participation, `coordination/coordination.cppm` for addressed requests and receipts, then `execution/execution.cppm` for tree execution, permitted observation bindings, tactical facts and command admission. `providers/providers.cppm` evaluates external-provider candidates from the same permitted inputs. Generic tree compilation/evaluation belongs to `behavior`; physical machinery belongs to `gameplay` and resources.

World composition supplies physical availability, permitted snapshots and domain actuation. Claims and Away policy have one mutable owner. Controller action continuations, binding evaluation, checkpoint validation, request identity and profile selection use independently compiled operations; the remaining World-bound execution fragments are migration work, not finished seams.
