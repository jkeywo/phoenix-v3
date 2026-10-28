# Mission objectives and debriefs

`phoenix.scenarios.objectives` owns mission checkpoint records, transition numbering/state-change admission, journal validation and authored debrief evaluation. World owns the ledger instance and captures immutable crew, compartment and personnel disposition rows; the compiled report builder evaluates scenario rules and objective/fact/history rows without native lookup or mutation.

The World adapter retains actor resolution, native fact capture and authoritative event emission at the existing explicit tick phases. Persistence preserves the original field names/defaults and candidate restore ordering. Relevant integration cases cover scenario conditions, mission reports, checkpoint rejection and exact continuation.
