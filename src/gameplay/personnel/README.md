# Personnel

`phoenix.gameplay.personnel.state` owns officer/roster state and its canonical saved representation. `phoenix.gameplay.personnel.runtime` owns duty lookup, assignment scoring and personnel travel progression from immutable native facts.

World gathers duty destinations from currently located teams, the accessibility of in-progress edges and open authored links after automatic assignment. The compiled movement phase then advances officers in roster order, preserving stun recovery, travel decrement, edge arrival and duty presence behavior. World retains station authority and command admission.

`phoenix.gameplay.personnel.clinical` owns the ordered clinical roster, officer/team patient projection, order filters and triage priorities. World supplies located personnel/team state and away eligibility. Severity, explicit patient priority and stable identity tie-breaking are applied in the compiled domain owner. Treatment payment and commit remain in the finite-work transaction path.

These modules do not own ECS handles or a World context and do not invoke callbacks into World. Existing personnel, medical, away and boarding integration fixtures remain the validation paths; no new save fields are added by clinical or movement snapshots.
