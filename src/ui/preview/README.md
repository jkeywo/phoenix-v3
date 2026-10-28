# Authoring-only Console preview

Start with [preview.cppm](preview.cppm).

Owns a synthetic PreviewSession, ConsoleSurface and render texture. The sample session grants no game authority. clear_console_preview runs before graphics/Rml teardown.

Dependencies: Content, Console surface, Rml runtime and Application composition for preview settings.

Validation: Workshop test isolation/verification plus explicitly triggered layout-editor preview smoke.
