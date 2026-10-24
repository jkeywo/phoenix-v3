# GUI controls and presentation

Player menus, lobbies and Consoles support controller navigation on native and browser builds. Workshop/GM retain keyboard and pointer navigation. Arbitrary text uses a physical or platform keyboard; there is no controller on-screen keyboard.

## Controller operation

| Input              | UI navigation                                                      | Flight mode                    |
| ------------------ | ------------------------------------------------------------------ | ------------------------------ |
| D-pad / left stick | Select controls spatially                                          | Existing flight bindings       |
| LB / RB            | Previous / next control in document order                          | Existing lateral controls      |
| South / A          | Activate; enter or commit numeric editing; hold momentary controls | Existing contact/camera action |
| East / B           | Cancel a numeric edit or close the current overlay                 | Existing camera back action    |
| Back / Select      | Toggle UI/Flight on flight Consoles                                | Toggle UI/Flight               |
| Start              | Open Settings                                                      | Existing impulse action        |

Menus, lobbies and non-flight Consoles default to UI navigation. Flight Consoles default to Flight. The displayed mode and help identify the current bindings. An unassigned controller can reach explicit assignment buttons before receiving a Console's operating controls. Selection stays local to its Console and does not move another Console's keyboard/reader focus. Activating a text field explicitly requests keyboard input there.

Numeric edits remain pending until South commits; East discards them. Hidden/disabled controls are excluded, and selected controls scroll into view. Stick navigation activates at 25%, releases below 15%, repeats after 400 ms and then at 100 ms intervals. Activation does not repeat. The pre-launch HTML shell uses ordinary DOM controls with the same timing policy; runtime browser/native input shares the C++ navigator. Browser file dialogs retain platform user-activation requirements.

Mode/authority changes, reassignment, disconnect, focus loss and invalidated controls cancel held actions. Sticks and buttons must return to neutral before resuming. Held inputs and controller selection are transient presentation state, never restored from a save.

## Text and tools

The existing player text setting scales Console text from 80–150%, including sensor, manual-flight, camera, fault and report text. Text-bearing controls grow or scroll. This does not implement the separate phone-layout redesign.

Workshop and standalone/embedded GM inspection offer **Presentation > Tool scale**, **Language** and **Pseudo-localisation**, including in read-only inspection. Scale defaults to 100%, with 75–200% choices in 25% steps. It scales the bundled LatoLatin font and style dimensions from a fixed baseline and persists with local-profile preferences. Older preferences default to 100%. Long property sections and lists have horizontal scrolling; docked panels remain resizable.

English and pseudo-localisation are the supported baseline. Tool labels/help and feedback templates use shared translation resources and stable ImGui identities. Feedback retains its source template and parameters, so changing presentation refreshes existing messages without translating filenames or recursively expanding parameter values. Supported font coverage includes accented Latin and common punctuation. This is not certification of Arabic/Hindi shaping, bidirectional editing or universal Unicode coverage. Ctrl+Y and Ctrl+Shift+Z both reach Redo.

## Windows accessibility

Providers retain semantic IDs and lifetime tokens across unrelated insertions, removal and reordering. Removing a control invalidates its old provider; recreating the same semantic ID creates a new lifetime. Session replacement invalidates previous providers and queued actions. Structure/property events publish after releasing provider locks; unrelated changes preserve focused control identity. Programmatic provider checks do not establish audible reader usability.

See [GUI verification evidence](smoke/gui-controls.md) for automated checks, manual journeys and remaining acceptance limits.
