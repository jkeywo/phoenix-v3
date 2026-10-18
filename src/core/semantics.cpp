module;
#include <array>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <vector>

module phoenix.navigation;

namespace phoenix
{
namespace
{
Json translations = Json::object();
std::string language = "en";
bool pseudo_language = false;
} // namespace
void configure_translations(const Json &catalog)
{
	if (catalog.at("schema") != 1 || !catalog.at("languages").is_object() || catalog.at("languages").size() > 64 ||
	    !catalog.at("languages").contains("en"))
		throw std::runtime_error("Invalid translation catalog");
	for (const auto &[locale, labels] : catalog.at("languages").items())
	{
		if (locale.empty() || locale.size() > 32 || !labels.is_object() || labels.size() > 4096)
			throw std::runtime_error("Invalid language resource");
		for (const auto &[key, text] : labels.items())
			if (key.empty() || key.size() > 256 || !text.is_string() ||
			    text.get_ref<const std::string &>().size() > 4096)
				throw std::runtime_error("Invalid translated label");
	}
	translations = catalog.at("languages");
}
void select_language(const std::string &locale, bool pseudo)
{
	if (locale != "en" && !translations.contains(locale))
		throw std::runtime_error("Language is not installed: " + locale);
	language = locale;
	pseudo_language = pseudo;
}
std::string translate(const std::string &key, bool pseudo)
{
	static constexpr std::pair<const char *, const char *> labels[] = {
	    {"home", "Home"},        {"host", "New game"}, {"join", "Join console"},
	    {"view", "Viewscreen"},  {"edit", "Workshop"}, {"locale", "Toggle pseudo-localisation"},
	    {"settings", "Settings"}};
	std::string result = key;
	for (const auto &[id, text] : labels)
		if (key == id)
			result = text;
	if (translations.contains(language))
	{
		const auto &labels = translations.at(language);
		if (labels.contains(key))
			result = labels.at(key);
		else if (labels.contains(result))
			result = labels.at(result);
	}
	if (pseudo)
		return "[ " + result + " ~~~ ]";
	return result;
}
std::string localise(const std::string &key)
{
	return translate(key, pseudo_language);
}
std::vector<SemanticControl> navigation_controls(const Application &app)
{
	std::vector<SemanticControl> controls;
	for (const char *id : {"home", "host", "join", "view", "edit", "locale", "settings"})
	{
		if (app.console_surface_active)
			break;
		if (std::string(id) == "host" && !app.can_host)
			continue;
		if (std::string(id) == "edit" && !app.can_edit)
			continue;
		if (std::string(id) == "settings" && !app.can_host)
			continue;
		controls.push_back({id, translate(id, app.pseudo_locale)});
	}
	for (const auto &[id, label] : app.screen_controls)
		controls.push_back({id, translate(label, app.pseudo_locale)});
	for (const auto &input : app.screen_inputs)
		controls.push_back(
		    {input.id, translate(input.label, app.pseudo_locale), {}, true, input.value, input.percentage});
	for (auto &control : controls)
		if (auto found = app.control_placements.find(control.id); found != app.control_placements.end())
			control.placement = found->second;
	return controls;
}
bool invoke_navigation(Application &app, const std::string &id)
{
	if (id == "settings" && app.can_host)
	{
		if (app.screen == Screen::settings)
			app.screen = app.settings_return;
		else
		{
			app.settings_return = app.screen;
			app.screen = Screen::settings;
		}
	}
	else if (id == "settings.close" && app.screen == Screen::settings && app.can_host)
		app.screen = app.settings_return;
	else if (id == "home")
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
bool set_music_volume(Application &app, const std::string &value)
{
	if (!app.can_host || app.screen != Screen::settings)
		return false;
	float percent = 0;
	const auto result = std::from_chars(value.data(), value.data() + value.size(), percent);
	if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(percent) ||
	    percent < 0 || percent > 100)
		return false;
	app.music_volume = static_cast<int>(std::lround(percent));
	return true;
}
} // namespace phoenix
