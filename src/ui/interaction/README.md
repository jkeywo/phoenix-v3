# Shared semantic and navigation records

Start with [interaction.cppm](interaction.cppm).

Standalone control/placement/semantic records describe permitted UI state. PadNavigation owns device-independent repeat/axis state; it receives the caller clock.

Dependencies: Portable foundation only. Application and platform adapters consume these records rather than own duplicate definitions.

`phoenix.navigation` is a separate compilation target in this folder. It builds application navigation controls and dispatches user actions through the application preference, display and connection interfaces. It reexports those composition contracts for existing front-end callers; portable interaction records have no application dependency.

Validation: Existing input-and-semantics system case and native/browser builds.
