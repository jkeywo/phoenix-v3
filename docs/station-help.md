# Station introductions and tab help

Player clients show an introduction after claiming a station in the lobby. It
follows the host's accepted complexity selection and gives responsibilities,
one sentence for each player-facing system, and one sentence describing AI work.
The eight authored stations are Courier Helm/Support and Cruiser Helm, Tactical,
Engineering, Science, Comms and Captain. Cruiser Comms and Captain remain fixed
at one star; the other six have three levels.

In game, `?` sits at the top right below the tabs. It opens help for the active
page and accepted workload, including auxiliary Console pages. Close, Escape or
controller Back dismisses it and restores focus to `?`. The popup is scrollable,
contains keyboard/controller focus and does not pause simulation. Each local
Console surface owns its own help state. A change to the Console, profile or
active page invalidates an open popup.

## Authoring

Console definitions may include `help` with `levels`, keyed by `"1"`, `"2"` and
`"3"`. Each level contains `responsibilities`, `automation`, a `systems` array
of `{id, label, text}`, and `tabs`, mapping fully qualified layout node/page IDs (for example `station.main`) to lists
of system IDs. `$console` is the whole-Console view. Node entries also cover
responsive layouts that turn containers into tabs. Names and descriptions use
the existing localisation resources, never markup or privileged world data.

`required_levels` optionally lists levels that must have complete layout-node
coverage. The shipped stations use this verification contract. Missing help
remains supported for existing third-party Consoles; supplied help is checked
for valid levels, text, unique system identities, page identities and references.
`default_level` may name the fixed workload of a Console without a runtime star profile; it never replaces an unavailable selected level. Ordinary definition inheritance applies. Help is included in lobby projections
and the public fields of restricted client asset packs.

Write self-contained guidance at each level, rather than referring players to
another level. System lists describe player-facing work; the station automation
sentence summarises assistance. Keep descriptions consistent with the current
fitting and admitted Controller commands. Updating workload layouts may require
adding node references; changing balance values should not require rewriting help.

## Verification

Synthetic content checks exercise inheritance, level selection, missing levels,
invalid pages/references and restricted client transfer. The lobby system case
checks help publication and refresh after replacing content.

The explicitly invoked native `phoenix_station_help_smoke base.zip
test-missions.zip` journey opens each station at its supported workloads, visits
available tabs and opens/closes their help. It writes representative captures to
`build/station-help-captures`. It is not an automatic shipped-content golden or
balance test. Actual screen-reader, touch/gamepad and six-display usability
remain separate platform acceptance checks.
