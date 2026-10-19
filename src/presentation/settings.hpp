#pragma once
namespace phoenix::settings_ui
{
struct Audio
{
	const char *channel, *menu_id, *label;
	int Application::*volume;
	inline std::string id(bool menu) const
	{
		return menu ? menu_id : std::string(channel) + "-volume";
	}
	inline std::string label_id(bool menu) const
	{
		return menu ? id(true) + "-label" : std::string(channel) + "-label";
	}
	inline std::string readout_id(bool menu) const
	{
		return menu ? id(true) + "-value" : std::string(channel) + "-percent";
	}
};
inline constexpr Audio audio[] = {{"master", "menu-master", "Master volume", &Application::master_volume},
                                  {"music", "menu-volume", "Music volume", &Application::music_volume},
                                  {"sfx", "menu-sfx", "SFX volume", &Application::sfx_volume}};
inline const char *audio_channel(const std::string &id)
{
	for (const auto &field : audio)
		if (id == field.id(false) || id == field.id(true))
			return field.channel;
	return "";
}
inline auto tabs(Application &app, std::string *focus = nullptr)
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
inline std::string audio_markup(bool menu)
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
inline void register_audio(Application &app, bool menu)
{
	if (app.settings_tab == "audio")
		for (const auto &field : audio)
			app.screen_inputs.push_back({field.id(menu), field.label, std::to_string(app.*field.volume), true});
}
inline void refresh_audio(const Application &app, Rml::ElementDocument &document, bool menu)
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
inline auto display_controls(const Application &app)
{
	return std::vector<std::pair<std::string, std::string>>{
	    {"settings.dust", app.dust_motes ? "Dust motes: On" : "Dust motes: Off"},
	    {"settings.grid", app.reference_grid ? "Reference grid: On" : "Reference grid: Off"},
	    {"settings.motion", app.reduced_motion ? "Reduced motion: On" : "Reduced motion: Off"}};
}
inline bool approval(const std::string &id)
{
	return id.starts_with("lobby.approve/") || id.starts_with("network.approve/") || id.starts_with("network.gm/");
}
inline bool session_action(Application &app, const std::string &id)
{
	if (!app.session)
		return false;
	if (id == "settings.away" || id == "settings.resume")
	{
		const auto view = app.session->console_observation(app.settings_console);
		return view && app.session->console_command(app.settings_console,
		                                            {view->own.id, "away", id == "settings.away" ? 1 : 0, {}});
	}
	if (id.starts_with("settings.profile/"))
	{
		const auto view = app.session->console_observation(app.settings_console);
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
inline auto approvals(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> result;
	if (game_active(app) && app.settings_tab == "session")
		for (const auto &[id, text] : app.session->lobby_controls())
			if (approval(id))
				result.emplace_back(id, text);
	if (game_active(app) && app.settings_tab == "session")
		if (const auto view = app.session->console_observation(app.settings_console);
		    view && view->gameplay.is_object())
			for (const auto &control : view->gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "away")
					result.emplace_back(control.value("value", 0) ? "settings.away" : "settings.resume",
					                    control.value("value", 0) ? "Away - hand control to AI" : "Resume control");
				else if (control.value("action", "") == "preset")
					result.emplace_back(
					    "settings.profile/" + control.at("id").get<std::string>(),
					    "Complexity: " + control.value("label", std::string{}) +
					        (view->gameplay.value("profile", std::string{}) == control.value("label", std::string{})
					             ? " (selected)"
					             : ""));
	return result;
}
} // namespace phoenix::settings_ui
