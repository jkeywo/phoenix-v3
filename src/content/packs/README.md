# Pack archives

Read types.cppm (phoenix.content.packs) first.

Immutable Pack identity, editable PackDraft, bounded ZIP/DAT encoding and JSON byte IO.

Dependencies: The independent image codec and portable data contracts.

Ownership: Pack contents and original archive storage are shared immutable values; editing creates a draft.

Validation: Content/archive integration and peer/network pack identity cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
