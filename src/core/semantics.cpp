#include <phoenix/semantics.hpp>
#include <array>
#include <utility>
namespace phoenix
{
std::string translate(const std::string &key, bool pseudo)
{
	static constexpr std::pair<const char *, const char *> labels[] = {
	    {"home", "Home"},       {"host", "New game"}, {"join", "Join console"},
	    {"view", "Viewscreen"}, {"edit", "Workshop"}, {"locale", "Toggle pseudo-localisation"}};
	std::string result = key;
	for (const auto &[id, text] : labels)
		if (key == id)
			result = text;
	if (pseudo)
		return "[ " + result + " ~~~ ]";
	return result;
}
std::vector<SemanticControl> navigation_controls(const Application &app)
{
	std::vector<SemanticControl> controls;
	for (const char *id : {"home", "host", "join", "view", "edit", "locale"})
	{
		if (std::string(id) == "host" && !app.can_host)
			continue;
		if (std::string(id) == "edit" && !app.can_edit)
			continue;
		controls.push_back({id, translate(id, app.pseudo_locale)});
	}
	for (const auto &[id, label] : app.screen_controls)
		controls.push_back({id, app.pseudo_locale ? "[ " + label + " ~~~ ]" : label});
	return controls;
}
bool invoke_navigation(Application &app, const std::string &id)
{
	if (id == "home")
		app.screen = Screen::home;
	else if (id == "host" && app.can_host)
		app.screen = Screen::server_lobby;
	else if (id == "join")
		app.screen = Screen::client_lobby;
	else if (id == "view")
		app.screen = Screen::viewscreen;
	else if (id == "edit" && app.can_edit)
		app.screen = Screen::workshop;
	else if (id == "locale")
		app.pseudo_locale = !app.pseudo_locale;
	else if (app.screen_action)
		return app.screen_action(id);
	else
		return false;
	return true;
}
} // namespace phoenix
