// Explicit graphical smoke; never part of the automatic system gate.
inline void probe_bridge_ui(phoenix::Application &app, int frames, int width, int height)
{
	auto require = [](bool ok, const char *message)
	{
		if (!ok)
			throw std::runtime_error(message);
	};
	auto has = [&](const std::string &id)
	{
		return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
		                   [&](const auto &row)
		                   {
			                   return row.first == id;
		                   });
	};
	auto invoke = [&](const std::string &id)
	{
		require(phoenix::invoke_navigation(app, id), ("Bridge UI action refused: " + id).c_str());
	};
	auto capture = [&](const char *phase)
	{
		TakeScreenshot(
		    ("build/bridge-" + std::to_string(width) + "x" + std::to_string(height) + "-" + phase + ".png").c_str());
	};
	if (frames == 65 || frames == 15)
	{
		require(app.control_placements.contains("viewscreen.readout/alert"), "Condition must have rendered bounds");
		const auto &condition = app.control_placements.at("viewscreen.readout/alert");
		std::cout << "Condition bounds: " << condition.x << "," << condition.y << " " << condition.width << "x"
		          << condition.height << " window " << GetScreenWidth() << "x" << GetScreenHeight() << std::endl;
		capture("bounds");
		require(condition.width > 0 && condition.height > 0 && condition.y >= 0 &&
		            condition.y + condition.height <= height,
		        "Condition must remain inside the ship frame");
	}
	if (frames == 65)
	{
		capture("view");
		require(has("settings") && !has("sidebar/close") && !has("console/31/settings"),
		        "Viewscreen must expose one settings entry and no sidebar toolbar");
		require(app.session->console_observation(31).has_value(), "Command sidebar must be claimed");
		for (const auto &[id, label] : app.screen_controls)
			require(label.find("Complexity") == std::string::npos, "Complexity leaked onto console");
		for (const auto &[id, label] : app.screen_readouts)
			require(id != "console/31/alert", "Duplicate sidebar condition");
		invoke("settings");
	}
	if (frames == 60)
		invoke("settings.tab/session");
	if (frames == 55)
	{
		require(has("sidebar/close") && has("sidebar/command") && app.settings_console == 31,
		        "Shared settings must retain sidebar selection and route");
		require(std::none_of(app.screen_controls.begin(), app.screen_controls.end(),
		                     [](const auto &row)
		                     {
			                     return row.first.starts_with("settings.profile/");
		                     }),
		        "Command must not expose complexity");
		require(app.control_placements.contains("sidebar/close") &&
		            app.control_placements.at("sidebar/close").height >= 20,
		        "Sidebar settings controls need visible bounds");
		capture("settings");
		invoke("sidebar/close");
	}
	if (frames == 50)
	{
		require(!app.session->console_observation(31), "Closing sidebar must release claim");
		invoke("sidebar/command");
	}
	if (frames == 45)
	{
		require(app.session->console_observation(31).has_value(), "Settings must reopen sidebar");
		invoke("settings.close");
	}
	if (frames == 35)
	{
		const auto choice = std::find_if(app.screen_controls.begin(), app.screen_controls.end(),
		                                 [&](const auto &row)
		                                 {
			                                 return row.first.starts_with("console/31/game.control/") &&
			                                        !app.screen_toggles.at(row.first);
		                                 });
		require(choice != app.screen_controls.end(), "Grouped stance button missing");
		invoke(choice->first);
	}
	if (frames == 25)
	{
		capture("selected");
		app.pseudo_locale = true;
		phoenix::select_language(app.language, true);
		std::cout << "PASS bridge layout, shared settings, sidebar lease and stance UI " << width << "x" << height
		          << '\n';
	}
	if (frames == 15)
		capture("expanded");
}
