# Player officer portraits

The twelve selectable player avatars use the approved heavy-ink Phoenix direction: bold vermilion, mustard and petrol blue, exaggerated expressions and unglamorous working faces. Generated individually with the built-in image_gen tool on 30 October 2026.

All wear the same petrol-blue cloth naval jacket with vermilion collar facings/tabs, mustard-gold piping, brass buttons, shoulder rank tabs and an ivory feather insignia. Neatness varies from immaculate closed collars to rumpled cloth, open fastenings and rolled sleeves.

| Slot | Age | Presentation                        | Background                    | Exaggerated trait           |
| ---- | --- | ----------------------------------- | ----------------------------- | --------------------------- |
| 1    | 67  | male                                | white                         | suspicion                   |
| 2    | 24  | male                                | Black                         | reckless confidence         |
| 3    | 48  | male                                | South Asian                   | unflappable calm            |
| 4    | 32  | male                                | East Asian                    | insatiable curiosity        |
| 5    | 69  | female                              | white                         | formidable disapproval      |
| 6    | 28  | female                              | Black                         | mischievous audacity        |
| 7    | 54  | female                              | South Asian                   | stubborn determination      |
| 8    | 37  | female                              | East Asian                    | infectious enthusiasm       |
| 9    | 62  | nonbinary, androgynous presentation | Black                         | bone-dry amusement          |
| 10   | 23  | nonbinary, androgynous presentation | Latine with medium brown skin | painfully earnest diligence |
| 11   | 43  | nonbinary, androgynous presentation | white                         | explosive impatience        |
| 12   | 58  | nonbinary, androgynous presentation | Southeast Asian               | obsessive precision         |

`avatar-1.png` through `avatar-12.png` are 48×48 PNGs within the 12 KiB player-profile limit. `portrait-1.png` through `portrait-12.png` are matching 192×192 display images. Existing native, browser and Console code loads these same numbered resources. Custom uploads remain supported.

[Preview](avatars-preview.png) is arranged in slot order, four columns by three rows. [Full-size masters and exact generation prompts](../../../docs/art/officer-avatars-v1/manifest.json) preserve the source work and character briefs. The mood board supplies style and palette; portrait 1 also supplied the shared uniform reference for most portraits. Portraits 5 and 7 were regenerated using the mood board alone to keep their faces distinct.

Run `./scripts/prepare-officer-avatars.ps1` on Windows to regenerate the two runtime sizes and preview from the committed masters. It resamples only; it does not redraw or retouch the artwork.

Verification (30 October 2026): all 24 runtime PNGs decoded, dimensions and byte limits passed, and all twelve network avatars have unique bytes. Largest avatar: 6,749 bytes. The 192px contact sheet was visually inspected. The existing native menu system executable passed. Browser launcher tests passed 37/38; the unrelated standalone-display-settings case expects only three fields while the current storage implementation also returns `message_muted` and `message_seconds`. No application code or tests were changed for this artwork replacement.

Profiles store their chosen image bytes rather than a preset index. Previously saved profiles therefore retain their earlier image until the player chooses one of these replacements in Profile; custom uploaded images are unaffected.
