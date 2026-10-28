# Controller coordination protocol

`phoenix.control.coordination` owns typed officer request support/selection and continuation validation. `coordination.cppm` is the interface; `coordination.cpp` evaluates stable request priority using an explicit simulation tick and validates restored peers against caller-supplied named identities.

World-side delivery and physical feasibility remain explicit integration work in `src/world/bindings/control_controller_coordination.cpp`. They use the same permitted Console snapshots and trusted knowledge admission as human messages. Protocol continuations preserve receipt ordering and request identities; callers do not edit another Controller's command section. Relevant integration: Controller coordination, handover and save/resume.
