# Application

This family connects front ends to portable contracts. `shell` owns the application composition state; `library` owns installed content, managed saves and user indexes; `preferences` owns portable preference data and localisation. Application-aware settings persistence, display assignments and connection readouts each have their own module.

The UI navigation dispatcher composes these services through their public interfaces. Link `phoenix_application_shell` for application state, `phoenix_ui_navigation` for semantic navigation and its application services, or `phoenix_console_model` for one permitted Console model. Each has its own named-module owner; navigation already carries the shell dependency.

Workshop authoring receives documents, frame inputs and callbacks. Graphics, console models and localisation do not depend on this familyâ€™s application state.
