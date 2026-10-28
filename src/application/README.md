# Application

This family connects front ends to portable contracts. `shell` owns the application composition state; `library` owns installed content, managed saves and user indexes; `preferences` owns portable preference data and localisation. Application-aware settings persistence, display assignments and connection readouts each have their own module.

The UI navigation dispatcher composes these services through their public interfaces. `phoenix_core` is only a compatibility link target: it has no source files or module ownership.

Workshop authoring receives documents, frame inputs and callbacks. Graphics, console models and localisation do not depend on this family’s application state.
