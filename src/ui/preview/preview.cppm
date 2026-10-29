module;
#include <raylib.h>
#include <memory>
export module phoenix.ui.preview;
export import phoenix.content.catalog;
export import phoenix.content.ui;

export namespace phoenix
{
Texture2D draw_console_preview(const Json &layout, int width, int height, Vector2 pointer, bool pressed, bool released,
                               float wheel, bool pseudo, std::shared_ptr<const Catalog> authoring = {},
                               int text_scale = 100);
const UiResolved &console_preview_geometry();
// Isolated sample state shown by Workshop; never authoritative gameplay.
Json console_preview_state();
Json console_preview_controls();
void clear_console_preview();
} // namespace phoenix
