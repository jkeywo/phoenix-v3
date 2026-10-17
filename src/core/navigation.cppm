module;
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.navigation;
export import phoenix.application;

export namespace phoenix
{
struct SemanticControl
{
	std::string id;
	std::string label;
	Application::ControlPlacement placement;
};
std::string translate(const std::string &key, bool pseudo);
std::vector<SemanticControl> navigation_controls(const Application &app);
bool invoke_navigation(Application &app, const std::string &action);
} // namespace phoenix
