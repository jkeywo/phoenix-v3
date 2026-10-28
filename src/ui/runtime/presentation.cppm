module;
#include <string>

export module phoenix.presentation;
import phoenix.application;

export namespace phoenix
{
bool probe_menu_keyboard(Application &app);
bool probe_gui_navigation(Application &app, unsigned stage);
bool probe_console_on_screen(ConsoleId route, const std::string &widget);
bool probe_console_helm(Application &app, ConsoleId route, bool verify = false);
bool probe_console_ready(ConsoleId route);
bool probe_console_manual(ConsoleId route, unsigned stage);
bool probe_console_report(ConsoleId route);
bool probe_console_readouts(ConsoleId route);
bool probe_console_tab(ConsoleId route, const std::string &action);
bool probe_console_fit(ConsoleId route);
bool probe_console_support_visuals(ConsoleId route);
bool probe_viewscreen_condition();
bool probe_console_navigation(ConsoleId route, unsigned stage);
bool probe_console_conversation(ConsoleId route);
bool probe_console_throttle(ConsoleId route, unsigned stage);
bool probe_console_engineering(ConsoleId route, unsigned stage);
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
bool probe_native_accessibility(Application &app);
} // namespace phoenix
