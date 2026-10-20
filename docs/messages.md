# Comic message presentation

Player Console and shared Viewscreen messages use the same presentation model: a sender name, portrait and automatically wrapping speech bubble. Message actions remain ordinary controls outside the bubble. The Viewscreen consumes public copies only. Neither visual prose nor accessible message names append technical routing IDs, tick counters, payload dumps or actionability labels. These fields remain in the underlying projections for admission, history and diagnostics.

Human message commands snapshot the sending lobby player's name and avatar. The snapshot travels through command serialization, delayed delivery and saved history, so later handovers or profile changes do not rename old messages. Legacy messages without a profile use their owning Console identity. Controller and physical reporter messages use the owning Console's authored, localised name.

The six AI portraits are recovered originals from the earlier built-in imagegen session, before the naval-uniform edits. `content/ui/messages/portraits.json` records source filenames and hashes. They are 192px display copies. Built-in mappings are Helm → 1, Engineering → 2, Science/Operations → 3, Comms/Communications/Support/Mission → 4, Weapons/Tactical → 5, and Captain/Command → 6. Custom Console IDs use a stable FNV-1a hash into the same six portraits. Missing or invalid player avatars use the Console portrait with the player's readable name.

Comic Neue Regular supplies bubble text. The unmodified font and its SIL Open Font License are bundled for native and browser use; existing multilingual fallback fonts remain loaded. Komika Text was preferred, but a verifiable bundled licence was not obtained, so the agreed OFL alternative is used. Missing translation keys or incomplete placeholders yield readable fallback text.

Message rows retain their element identity while new messages arrive. The existing containing panel scrolls; bubbles grow with text and have no manual resize controls. The message renderer does not impose the old additional eight-message Viewscreen limit. Authoritative history and network transfer limits still apply.

## Validation

`system.gameplay_knowledge` exercises synthetic content through the production loader and World, checking delayed sender snapshots, save/restore and handover, prose-only semantics, private/public filtering, stable Console portraits, invalid-avatar fallback and missing translations. `system.controller_runtime` checks actual public-message publication and the separate public presentation model.

The explicitly invoked `phoenix_messages_smoke` target renders the production message component at wide and narrow sizes, including long text, enlarged type, multilingual fallback and escaped markup. It checks unchanged row identity, focus and scroll after arrival and captures `messages-wide.png`, `messages-narrow.png` and `messages-scrolled.png`. It is excluded from ordinary builds and CTest. A rendered/semantic probe does not establish actual screen-reader or physical-device acceptance.
