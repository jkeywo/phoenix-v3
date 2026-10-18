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
	bool editable = false;
	std::string value;
	bool percentage = false;
};
std::string translate(const std::string &key, bool pseudo);
void configure_translations(const Json &catalog);
void select_language(const std::string &language, bool pseudo = false);
std::string localise(const std::string &key);
std::vector<SemanticControl> navigation_controls(const Application &app);
bool invoke_navigation(Application &app, const std::string &action);
bool set_music_volume(Application &app, const std::string &value);
} // namespace phoenix
