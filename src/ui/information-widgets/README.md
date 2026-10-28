# Conversation/message presentation

Start with [messages.cppm](messages.cppm).

Message functions preserve mounted Rml rows and scroll/focus while updating permitted text/assets. Metadata is returned to an explicit readout sink, not Application.

Dependencies: Console model records, Rml runtime and localisation.

Validation: Existing conversation/knowledge/session cases and explicitly triggered Comms UI smoke.

`conversations.cppm` (`phoenix.ui.conversation_panel`) owns conversation browsing, mounted-view signatures, read acknowledgements, refresh/fit and actions. Its borrowed frame supplies document services, semantic output destinations, immutable content and explicit visibility/settings facts. It imports no Application or mutable session. Incoming-text invalidation follows the host's text-cache resets; focus, scroll, acknowledgement and split/narrow composition order are preserved.
