# Application composition

`phoenix.application` declares the state and callbacks joining the front ends to session, menu and interaction contracts. It also owns screen names and the active-game predicate.

The application is a composition boundary. Workshop editors, renderers, portable localisation and authoritative gameplay use narrower inputs instead of importing this state. Preference storage, display assignment and connection readouts have separate modules in this family.
