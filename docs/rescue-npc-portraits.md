# Rescue NPC portraits

The rescue cast uses 40 distinct 192px portraits: 17 military officers shared by role between the Courier and Cruiser, nine Morrow civilians, eight shared raider crew types, three individual raider captains and three Cairn civilians. These are independent of the twelve human-player avatars.

[Contact sheet](art/rescue-npc-v1/contact-sheet.png) follows the order in the [portrait manifest](art/rescue-npc-v1/manifest.json), eight columns by five rows. The manifest records each name, department, age, presentation, personality art brief, exact built-in image_gen prompt, master filename and runtime asset reference. Full-size generated masters are stored beside it. `scripts/prepare-rescue-portraits.ps1` resamples those masters into `content/base/assets/personnel/rescue/`; `-AvailableOnly` supports incremental installation during authoring. Normal completion requires all forty masters.

## Department appearance

Uniform similarities apply **within**, not between, military departments. Sailing has blue high-collared watch jackets; Tactical has charcoal tunics with scarlet collars and reinforced cloth shoulders; Sciences has ivory asymmetric instrument coats with teal lapels; Signals has vermilion waistcoats and communications headsets; Passage has a plum wrap-coat, ivory mantle and navigator apparatus; Medical has ivory coats with navy rounded collars; Engineering has mustard canvas work jackets with blue collars and repaired seams. Expressions and personal grooming carry the personality, independently of ethnicity.

Morrow wears civilian merchant jackets and knitwear. Cairn uses its own teal-grey station workwear with orange collars and ivory shoulder panels. Raiders wear outlandish patched and salvaged clothing. Gameplay traits, qualifications, powers and officer IDs are unchanged; personality briefs describe the artwork rather than new modifiers.

Captain Vale and Engineer Orin are Morrow's helm and first repair officers respectively; their Comms identities use the same names and portraits. Cairn's Receiving Officer Fen Toller, Controller Ada Neme and Engineer Ren Sato have exclusive portraits. Military Comms placeholders now use Idris Holt and Chief Rao rather than civilian names.

## Raider captains

The Courier encounter uses **Brass Promise**, commanded by **Captain Hesta Brass**. The Cruiser encounter additionally includes **Quiet Courtesy**, commanded by **Captain Corvin Vell**, and **Last Chance**, commanded by **Captain Kit Spar**. All three inherit the same raider fit and share the eight crew portraits, but use separate crew names and captain identities.

The optional interdiction `attacker_character` identifies a character on every authored attacker variant. Rescue uses `captain`; omission preserves the legacy ship endpoint. The active attacker is the dialogue speaker. When a surviving ship takes over, future dialogue binds to its captain while existing messages retain their original name and portrait. Hailing a raider's ship endpoint introduces its captain; the introduced captain can then be hailed directly. Workshop exposes the optional field with the other encounter bindings.

## Verification

All 40 finished runtime PNGs have unique bytes, are 192 by 192 pixels and fit the 256 KiB personnel limit. All 40 are referenced by production content, with no missing portrait paths and no temporary fallback images remaining. The completed 192px contact sheet was visually inspected. Existing human-player avatar files were not modified.

The native and WebAssembly interdiction integration executables pass, including synthetic captain-based demands, discovery/hailing, active-attacker changes, historical message snapshots, missing-character rejection, legacy ship-endpoint encounters and exact save/restore continuation. The new tests use independent synthetic fixtures and a test-only pixel portrait, not these authored artworks. The native application rebuilt, and both `build/native/base.dat` and the ordinary bundled `build/native/base.zip` passed production pack verification after all forty replacements were installed.
