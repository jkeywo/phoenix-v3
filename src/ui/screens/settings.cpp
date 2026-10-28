module;
#include <cstdint>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.settings;

namespace phoenix::settings_ui
{
const char *audio_channel(const std::string &id)
{
	for (const auto &field : audio)
		if (id == field.id(false) || id == field.id(true))
			return field.channel;
	return "";
}
std::vector<std::pair<std::string, std::string>> tabs(Application &app, std::string *focus)
{
	auto result = settings_tabs(app);
	if (std::none_of(result.begin(), result.end(),
	                 [&](const auto &tab)
	                 {
		                 return tab.first == "settings.tab/" + app.settings_tab;
	                 }))
	{
		app.settings_tab = "audio";
		if (focus)
			*focus = "settings.tab/audio";
	}
	return result;
}
std::string audio_markup(bool menu)
{
	std::string markup;
	for (const auto &field : audio)
	{
		const auto id = field.id(menu);
		const auto tag = menu ? "p" : "span";
		markup += "<label id=\"" + field.label_id(menu) + "\" for=\"" + id + "\">" +
		          Rml::StringUtilities::EncodeRml(localise(field.label)) +
		          "</label><input class=\"audio-volume\" id=\"" + id +
		          "\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"50\"/><" + tag +
		          " class=\"audio-percent\" id=\"" + field.readout_id(menu) + "\">50%</" + tag + ">";
	}
	return markup;
}
void register_audio(Application &app, bool menu)
{
	if (app.settings_tab == "audio")
		for (const auto &field : audio)
			app.screen_inputs.push_back({field.id(menu), field.label, std::to_string(app.*field.volume), true});
}
void refresh_audio(const Application &app, Rml::ElementDocument &document, bool menu)
{
	for (const auto &field : audio)
	{
		const auto value = std::to_string(app.*field.volume);
		if (auto *slider = dynamic_cast<Rml::ElementFormControl *>(document.GetElementById(field.id(menu))))
			if (slider->GetValue() != value)
				slider->SetValue(value);
		if (auto *label = document.GetElementById(field.label_id(menu)))
			label->SetInnerRML(Rml::StringUtilities::EncodeRml(localise(field.label)));
		if (auto *readout = document.GetElementById(field.readout_id(menu)))
			readout->SetInnerRML(value + "%");
	}
}
std::vector<std::pair<std::string, std::string>> display_controls(const Application &app)
{
	return std::vector<std::pair<std::string, std::string>>{
	    {"settings.dust", app.dust_motes ? "Dust motes: On" : "Dust motes: Off"},
	    {"settings.grid", app.reference_grid ? "Reference grid: On" : "Reference grid: Off"},
	    {"settings.motion", app.reduced_motion ? "Reduced motion: On" : "Reduced motion: Off"}};
}
bool approval(const std::string &id)
{
	return id.starts_with("lobby.approve/") || id.starts_with("network.approve/") || id.starts_with("network.gm/");
}
bool session_action(Application &app, const std::string &id)
{
	if (!app.session)
		return false;
	if (id == "settings.away" || id == "settings.resume")
	{
		const auto view = app.session->console_snapshot(app.settings_console);
		return view && app.session->console_command(app.settings_console,
		                                            {view->own.id, "away", id == "settings.away" ? 1 : 0, {}});
	}
	if (id.starts_with("settings.profile/"))
	{
		const auto view = app.session->console_snapshot(app.settings_console);
		if (!view)
			return false;
		for (const auto &control : view->gameplay.value("controls", Json::array()))
			if (control.value("action", "") == "preset" && control.value("id", "") == id.substr(17))
				return app.session->console_command(
				    app.settings_console, {view->own.id, "preset", control.at("value").get<std::int64_t>(), {}});
		return false;
	}
	return app.session->session_action(id);
}
std::vector<std::pair<std::string, std::string>> approvals(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> result;
	if (game_active(app) && app.settings_tab == "session")
		for (const auto &[id, text] : app.session->lobby_controls())
			if (approval(id))
				result.emplace_back(id, text);
	if (game_active(app) && app.settings_tab == "session")
		if (const auto view = app.session->console_snapshot(app.settings_console); view && view->gameplay.is_object())
			for (const auto &control : view->gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "away")
					result.emplace_back(control.value("value", 0) ? "settings.away" : "settings.resume",
					                    control.value("value", 0) ? "Away - hand control to AI" : "Resume control");
	return result;
}
} // namespace phoenix::settings_ui
