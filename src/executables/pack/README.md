# Verified command-line pack creation

`pack.cpp` walks a source directory, enforces finite entry limits, builds a PackDraft through production resolution, writes the selected archive encoding and emits launcher/companion metadata. It imports narrow packs, catalogue, verification and distribution APIs rather than owning their implementations.

Dependency direction is toward content compilation only. The executable does not run a World or instantiate graphics. Symlink rejection, ordered dependency inputs and final re-verification preserve the same content identity rules used by Workshop and verification.

Content-and-packs cases exercise the production archive/resolution paths. Command-line generation checks use synthetic fixtures or explicit production verification without freezing tuning. Current native/browser compilation and content checks are recorded in migration evidence.
