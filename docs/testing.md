# Phoenix V3 testing policy

Status: design policy, 17 October 2026. The suite is not implemented yet. Follow this policy when building the [selected stack](technical-stack.md) and the gameplay in the [GDD](GDD.md).

Tests protect intended behavior and runnable data. They must not turn each content asset, tuning value, implementation function or historical output into a permanent constraint. Use three categories only; choose a test because it detects a meaningful failure, not to satisfy a coverage percentage or add one test per change.

## The three categories

| Category           | Question answered                                                     | Data                                                                    | Normal execution                                                                                 |
| ------------------ | --------------------------------------------------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Smoke              | Can we play this journey, and what happens on this build/machine?     | Named authored content or a deliberate stress setup                     | Explicit manual invocation; may run automation, scripts or recorded input after being triggered. |
| Verification       | Is this authored data coherent and runnable under its declared setup? | Actual ships, equipment, trees, scenarios and packs                     | Automatic on changed content and ordinary CI; validate the affected dependency closure.          |
| System integration | Does this system still obey its behavioral contract?                  | Purpose-built synthetic fixtures through the real loader/execution path | Fast repeatable automated runs, normally headless, in CI.                                        |

A smoke run can report a defect and support a release decision. It is not a compulsory playthrough/performance suite on every edit. Verification does not prove balance or fun. A synthetic system test does not prove a particular shipped ship is well balanced. Keep those claims separate.

## 1. Smoke: manually invoked evidence

Record representative playthroughs, crew journeys, native six-screen runs, browser journeys, screen-reader operation and performance captures. Runs can be manually played or automated after an explicit user/developer trigger. Fixed seeds/input recordings help comparisons but do not freeze historical outcomes.

Capture the build/content revision, scenario/setup, seed where relevant, input method and machine/browser/display configuration. Keep logs, measurements and optional recordings useful for diagnosis. For performance, compare like-for-like runs and report distributions/spikes and simulation/UI/GPU costs; do not fail arbitrary CI machines against somebody else's wall-clock result.

The six-1080p-screen/60-Hz target and player screen-reader usability require actual appropriate platform runs. Synthetic tests can protect the routing and semantic contracts but cannot stand in for these smoke checks. Combat/rescue walkthroughs in the GDD are acceptance scenarios, not a requirement to record them on every PR.

Do not import V2's old smoke assertions, balance thresholds or whole-world expected fingerprints by default. A smoke recording is evidence tied to its setup, not a golden file that makes every later balance change fail. Store large captures outside ordinary source-controlled fixtures; include a concise receipt or artifact link where useful.

## 2. Verification: authored content can run

Use the production loader and definition-resolution rules. Verify schemas, required fields, types/ranges, references, inheritance, compatible interfaces, allowed dependency cycles, bindings, ownership and supported node/command vocabulary. Validate resolved definitions, not just the spelling of source documents. Check duplicate IDs, missing resources, incompatible fits, unresolved service names and genuinely unsupported combinations.

Add semantic checks for actual domain contracts. For example, verify a ship's power grid by checking:

- Sources, local stores and links exist, resolve to compatible providers/consumers and have valid finite parameters.
- Fitted energy-consuming capabilities have supported supply paths/local reserves where their declared operation needs them.
- Command channels, relevant controls/AI and Console bindings resolve; no conflicting control ownership is introduced.
- The declared normal starting configuration can load/initialise and perform required baseline operation, or explicitly identifies a deliberate disabled/damaged/unsupported starting state.
- Material, thermal and service definitions are coherent without assuming every optional weapon and boosted engine must run at maximum simultaneously.

A finite loader/initialisation/readiness probe is appropriate when static validation cannot establish a declared requirement. Check capability/readiness/diagnostics rather than run a full mission or infer universal balance limits. A deliberately powerless wreck, isolated compartment or damaged mission start is valid when the authored scenario makes that state intentional; do not weaken ordinary errors globally to accommodate it.

**Do not assert that the Cruiser travels at 30 m/s, has a particular battery capacity, produces a fixed damage rate or matches an old serialised snapshot.** Verify that its currently authored parameters are valid and connected. If speed changes from 30 to 35 and all declared contracts remain satisfied, verification should pass without updating a test.

Use errors for data that cannot satisfy a required contract. Use actionable warnings for suspicious but potentially intentional configurations. Diagnostics identify the definition, field/binding and reason. Any supported intentional-exception mechanism must be narrow and explicit rather than silently exempting an entire content class. The concrete encoding is selected with the content format.

Tree verification covers supported contexts, transitions, references, resolved inheritance and typed bindings. Away-decision nodes need a usable timeout/default branch. UI verification checks translation references, declared bindings/actions and semantic information required by supported player widgets. These checks establish structural completeness, not the quality of a translation or screen-reader experience.

Recheck affected dependency closures on ordinary changes. Verify the complete authored corpus on pack/release preparation and shared-loader/schema changes. Data checks must not be duplicated into per-ship code tests. A change to the validation rules may need a small synthetic system integration case proving the rule, while normal content is simply verified against it.

## 3. System integration: behavioral contracts with synthetic data

"System integration tests" is the project term. Each test loads a minimal purpose-built setup through the real content loader, initialises the relevant mechanisms and exercises admitted commands, work and observations. Use real Flecs execution and the same supported code as the game. Avoid a mock simulation, direct hidden-state edits as a substitute for the tested action or a parallel test-only implementation.

Tests focus on a behavior boundary: weapons, engines, shields, stores, repair, state-tree evaluation, job execution, knowledge/admission, save/resume, input routing or UI semantics. They do not require every implementation function or class to acquire its own test. A small cross-System scenario belongs here when the contract itself crosses that boundary, such as an engine consuming energy supplied through a real link.

Synthetic fixtures may inherit from a small shared test-only core. The core supplies boring valid scaffolding: identities, clock/seed, basic ownership, minimum entity definitions and explicit services. It must not inherit from the production Cruiser, default mission pack or balance catalogue. Fixture-specific behavior and parameters remain visible in the test or its local fixture. Avoid a giant shared world whose incidental edits break unrelated cases.

The declared synthetic values are controlled experiment inputs. Exact expectations derived from those inputs are legitimate: if a fixture command requests 10 units of energy and the contract grants it, consumption of 10 can be asserted. That does not freeze any production weapon's energy cost. Where physics uses floating point, use a tolerance justified by the behavior, not a snapshot of every intermediate value.

Observe through public supported results/observations where that is the contract. Inspect physical state where testing conservation or internal execution is specifically the concern. A Console-knowledge test must not obtain a pass by reading unrestricted truth; an energy-conservation test should account for the actual resource owner. Keep assertions at the intended boundary.

### Representative contracts

| System/behavior     | Synthetic setup and assertion                                                                                                                                                                   | Avoid                                                                       |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| Engine              | Known thrust/mass/energy fixture accelerates appropriately; disabled or unpaid operation produces no unsupported thrust; release stops extra thrust without erasing velocity.                   | A fixed top speed copied from the shipped Cruiser.                          |
| Weapon              | Known geometry/ammunition/reserve fixture fires when eligible, consumes its configured costs and applies supported impact; insufficient resources/refused authority causes no forbidden effect. | Current production damage-per-second or a recorded duel outcome.            |
| Shields             | Hits affect the appropriate arc; regeneration spends actual energy; redistribution passes through the battery rather than teleporting protection.                                               | Exact shipped shield capacities or a whole-ship balance snapshot.           |
| Logistics           | A dispatched batch leaves source stock, reserves receiving space and arrives once; blockage/cancellation preserves real quantity and location.                                                  | A golden copy of every production hold and route.                           |
| State trees/jobs    | Exercise ordered outputs, inherited/reference behavior and admitted stage work; interrupted work preserves the supported commitments/effects.                                                   | One test per node occurrence in every authored mission.                     |
| Away operation      | A synthetic tree surfaces a Comms decision, follows an answer or timeout default, continues during link loss and resumes without repeated outcomes.                                             | Locking the starter mission's plot, timer values or preferred ending.       |
| Knowledge/control   | Sharing a permitted finding does not transfer machinery authority or reveal other Console facts.                                                                                                | Exposing hidden world state merely to simplify assertions.                  |
| Save/resume         | Resume a synthetic operation at a consequential boundary and compare its supported continuation, quantities and outcome with an uninterrupted run.                                              | Fixed historical whole-world hashes tied to production content.             |
| Input routing       | Two console identities receive simultaneous touch/gamepad actions independently; device removal clears held input without granting another operator authority.                                  | Requiring six physical monitors for every CI run.                           |
| Player UI semantics | Known permitted contact/control appears with equivalent semantic name, state and action; hidden facts stay absent.                                                                              | Treating a semantic-tree check as proof that NVDA/VoiceOver actually works. |

Choose representative success, refusal and interruption/recovery cases according to the behavior's actual risk. Do not multiply every fixture across every ship, preset, save point and possible dependency. Include those dimensions only when they exercise distinct rules.

## Execution and maintenance rules

The ordinary automatic gate builds the relevant targets, verifies affected content and runs relevant system integration tests. Run the full bounded system suite in normal CI once it exists; heavy playable/performance/device journeys remain explicit smoke runs. Build/toolchain checks are prerequisites, not a fourth gameplay-test category. Select exact tools/commands and practical runtime budgets when the harness is implemented.

Do not require universal unit-test coverage, one regression test per edit, one test per function, code-mirroring assertions or snapshot updates whenever content changes. A narrowly isolated helper assertion can support a meaningful system contract, but should not grow an independent mandatory unit-test programme.

Use deterministic ticks/seeds for synthetic behavior and stable fixture ordering. Test reproducibility or uninterrupted-versus-resumed equivalence within the controlled setup rather than preserving all historical fingerprints. Exact stable formats/ordering need dedicated assertions only when they are an intentional compatibility/behavior contract, not because their current output is easy to snapshot.

When a rule intentionally changes, update its focused fixture and expected behavior together, explaining the changed contract. When tuning changes, verification should normally pass unchanged. When an internal refactor preserves a contract, the system test should normally remain unchanged. If it does not, examine whether it depended on incidental internals before adding more assertions.

For a bug, add or adjust a regression case when it protects a distinct important behavior or an expensive-to-repeat failure. Prefer extending an existing focused fixture over creating a large new scenario. Remove redundant or superseded tests when the protected contract moves; do not keep both implementations' test suites by default.

Keep synthetic fixtures, shipped authored content and smoke evidence separate. Test results should name the violated contract; content errors should name the bad definition; smoke reports should identify the observed build/setup. Never relabel a previous failure as a pass or claim a manual/device check ran because an adjacent automated check passed.
