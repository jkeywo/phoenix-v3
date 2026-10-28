# Display assignments

`phoenix.application.displays` restores station-to-monitor preferences and reconciles monitor enumeration with the application's active station. It receives the application at the composition boundary and uses the public session lobby contract; it owns no graphics window or authoritative simulation state.

Monitor enumeration and window placement belong to the native adapter. Persistent identities and the existing preference representation remain unchanged.
