module;
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.presentation;
export import phoenix.navigation;

export namespace phoenix
{
void open_window();
bool close_requested();
bool graphics_has_keyboard_focus();
void draw_screen(Application &app);
void close_window();
} // namespace phoenix

export namespace phoenix
{
void publish_semantics(const Application &app);
void drain_semantic_actions(Application &app);
void install_native_accessibility(void *window);
void remove_native_accessibility(void *window);
bool probe_native_accessibility(Application &app);
} // namespace phoenix
