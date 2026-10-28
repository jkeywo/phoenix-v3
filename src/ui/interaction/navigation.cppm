module;
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.navigation;

export import phoenix.application;
export import phoenix.ui.interaction;
export import phoenix.application.preferences;

export import phoenix.application.preference_store;
export import phoenix.application.displays;
export import phoenix.application.connections;

export namespace phoenix
{
std::vector<SemanticControl> navigation_controls(const Application &app, bool include_consoles = true);
bool invoke_navigation(Application &app, const std::string &action);
} // namespace phoenix
