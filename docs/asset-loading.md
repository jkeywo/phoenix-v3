# Packaging and loading assets

Workshop's **File > Package…** menu exports a **Game pack** or the **Selected scenario**, as **Source ZIP…** or **Compiled DAT…**. Packaging runs inside native/browser Workshop; authors do not need a separate executable. Exporting leaves the ordinary Save destination unchanged. Native uses a save picker; browsers use File System Access where available and otherwise download a copy.

ZIP retains editable JSON and PNG files. DAT uses the `PHXDAT1` version signature followed by a bounded ZIP directory with entry CRC checks. `definitions.json` contains a `PHXCBR1`-prefixed CBOR record; PNG paths contain `PHXRGBA1` records with little-endian dimensions and RGBA8 pixels. Logical asset references stay the same. Text manifest and launcher metadata remain available before WASM starts. Packages retain the 32 MiB transfer limit; DAT entries permit a full 16-megapixel RGBA image and the expanded package is limited to 256 MiB. Unsupported versions, damaged entries, invalid dimensions and excess bounds are refused.

Both encodings use the production verifier and exact archive SHA-256 identities. Workshop opens DAT as editable definitions/images; ZIP export restores JSON/PNG. Native `--game-pack` and `--workshop-pack` also accept source directories. A directory opened in Workshop uses Save As for its first saved archive. Browser imports accept ZIP or DAT. Installed-library filenames are internal storage keys and do not determine encoding. Saves and transfers retain exact loaded package bytes.

A scenario export is a small overlay with a distinct generated ID, a dependency on the current game pack, and an explicit override for the selected map. Package/install the game pack first, then its scenario. Shared definitions and images come from that prerequisite. The scenario's launcher metadata lists the selected map.

## Preparation before play

Host preparation follows resolved references from the selected map through every allowed ship, fitted definition, Console, tree, situation, spawn group and image. It includes future literal spawns and explicitly declared dynamic dependencies. Unrelated scenarios' assets are omitted. A map can declare `"preload": ["my-pack.extra-ship", "my-pack:assets/extra.png"]`; references must resolve. Dynamic entity selection requires this declaration. Authors must include every possible dynamic selection; with an explicit preload list, runtime spawns outside the resolved definition set are refused.

Preparation uploads textures and prepares sprite materials on the graphics thread, yielding between resources. Texture owners share identical image payloads across the scene and local Consoles. Built-in Console artwork and navigation rendering resources are prepared during loading. Scenario resources remain retained for later spawns and are released with their owner.

Remote crew clients first receive a small Console-selection package. After a claim commits, the host offers a DAT package for the actual assigned Console set, including additional Consoles supplied by its profile, containing public bindings/layout metadata and public radar images. Detailed hull graphics, maps, private trees and unassigned Consoles are excluded. Changing claims produces a fresh offer. Browser client runtime data excludes bundled gameplay ZIP/DAT files. GM inspection retains its separate authorised content path.

The loading screen covers content transfer and graphics preparation before showing the Console or Viewscreen. It uses the existing space panorama, phase text, progress bar, accessible status and cancellation. Workshop's **Pack loading background** and **Scenario loading background** image pickers allow overrides; scenario artwork takes precedence. Approved clients receive that artwork in their permitted presentation package. The HTML launcher has a matching startup background/progress display while WASM loads.

## Validation

Synthetic production-loader cases cover ZIP/DAT equivalence, pixel preservation, editable imports, damaged/versioned input, dependent scenario packaging and Console selection isolation. Existing content, session, network, Workshop and WASM cases exercise their real loaders. Graphical loading, cancellation and file-picker checks are manually triggered evidence; headless tests do not establish reader, physical-device or performance acceptance.
