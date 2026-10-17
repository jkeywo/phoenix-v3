# Source guide

Located and assembled on 17 October 2026 for the Phoenix V3 rewrite.

## 1. Existing Phoenix and old GDD

Local template: `C:\Coding\project-phoenix-v2`.
Remote: https://github.com/jkeywo/project-phoenix-v2.

The old GDD is a federated collection under `docs/gdd`, not a single monolithic document. Begin with its `index.md`, then `foundation/current-baseline.md`, `foundation/overview.md`, and `future/roadmap-baseline.md`. Its Thin Margin setting and old ship/scenario catalogues are comparison material for the rewrite.

The preserved copy is under [references/phoenix-v2/docs/gdd](../references/phoenix-v2/docs/gdd/index.md). The original code and assets remain in V2 as the implementation template.

## 2. Recent redesign and 24-increment plan

The redesign lives in `docs/gameplay-redesign`. The ordered review covers **G001-G114 and S001-S102: 216 systems**. The two catalogues, research register, architecture decisions and mechanism reconciliation explain how the design developed.

The execution plan is `implementation/index.html` and `implementation/migration-ledger.json`, covering increments 0-24. Increment 0 has `baseline.md`; increment 1 is documented through `console-baseline.md`, `console-validation.json` and the Console ownership design. Separate `increment-2.md` through `increment-24.md` hold the subsequent detail. The revised increments 22-24 replace earlier summaries. The adopted V2 design and roadmap YAML are also preserved under `references/phoenix-v2/pasm/spec` for comparison; this is not a complete runnable PASM workspace.

The status record distinguishes implementation from acceptance: increment 0 is formally closed; 1-7 have initial automated exits; 8-12 have substantial implementation and verification but remaining acceptance; 13-24 contain partial work and incomplete capabilities. Patrol, War and Sandbox are not delivered. Do not infer completion from an increment file existing.

The snapshot includes working-tree versions. At collection, `implementation/README.md` and `implementation/verification-8-12.json` had local modifications relative to V2 HEAD. Exact source hashes, HEAD and status are recorded in the manifest. V2 may continue changing after this snapshot.

Original execution PRD: https://github.com/jkeywo/project-phoenix-v2/issues/1562.

## 3. Settled Dark pivot

The decisive design work is in the Codex conversation **Explore Settled Dark setting pivot**, thread `01a13b70-2aef-7453-93fe-453c47eecd7c`. Its completed 18-issue review is preserved in [settled-dark-direction.md](settled-dark-direction.md).

Use the final review: **immediately before the conquest**, Caelifen household service, an original frontier district, psychic EWar, navigator capacity shared across passage operations, and the Courier/Cruiser starter foundation. The earlier suggestion of several centuries before the conquest was superseded.

Source locations:

| Location | Role |
|---|---|
| `C:\AnalogueGames\analogue-projects\GameProjects\_shared_worlds\settled_dark` | Shared world bible and chronology; discovered through the Void Throne dependency. |
| `C:\AnalogueGames\TheVoidThrone` | Original setting, factions, regions and hidden lore. |
| `C:\AnalogueGames\analogue-projects\GameProjects\void_throne` | Older structured game package referencing the shared world. |
| `C:\AnalogueGames\void-throne\lore` | Recent scenario additions and source inventory; distinguish these from shared canon. |

The reference snapshot includes the shared world, original setting/faction/region/lore Markdown and the recent scenario inventory. The Void Throne describes a later historical period: its post-conquest political state and scenario inventions must not be copied into Phoenix's pre-conquest present without a deliberate adaptation.

## Starting boundary

The task establishes a fresh repository and gathers its inputs. It does not resume V2's paused implementation or adopt its technical stack and delivery sequence wholesale. Reconcile the old GDD, reviewed redesign and final setting decisions into a V3 design before implementation.
