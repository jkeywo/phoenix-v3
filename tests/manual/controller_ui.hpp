// Explicitly invoked graphical smoke using the synthetic controller_ui.py packs.
// Run with --controller-report-smoke and the synthetic base/mission/seat options.
inline bool probe_controller_reports(phoenix::Application &app, int frames, bool timed_out)
{
	const auto pending = [timed_out](const std::string &reason)
	{
		if (timed_out)
			throw std::runtime_error(reason);
		return false;
	};
	if (frames == 52 || frames == 42)
	{
		// Stable descriptor actions for profile.automatic and profile.direct.
		const auto profile = frames == 52 ? "70726f66696c652e6175746f6d61746963" : "70726f66696c652e646972656374";
		if (!phoenix::invoke_navigation(app, std::string("console/1/game.control/") + profile))
			throw std::runtime_error("Console Settings complexity action refused");
	}
	if (frames == 44 || frames == 34 || frames == 26 || frames == 18)
	{
		const bool human = frames == 34 || frames == 18;
		const bool away = frames == 26;
		const std::string profile = frames == 44 ? "automatic" : "direct";
		const auto observed = app.session->console_observation(1);
		if (!observed || observed->gameplay.at("profile") != profile)
			return pending("Console Settings complexity did not commit: " + profile);
		const auto &controls = observed->gameplay.at("controls");
		const auto away_control = std::find_if(controls.begin(), controls.end(),
		                                       [](const auto &control)
		                                       {
			                                       return control.at("action") == "away";
		                                       });
		if (away_control == controls.end() || away_control->at("value") != (away ? 0 : 1))
			return pending("Console Settings Away state did not commit at frame " + std::to_string(frames));
		unsigned commands = 0;
		for (const auto &controller : observed->gameplay.at("controllers"))
			for (const auto &[channel, command] : controller.at("commands").items())
			{
				++commands;
				if (command.at("human") != human)
					return pending("Console Settings assigned the wrong operator for " + channel + " at frame " +
					               std::to_string(frames));
			}
		if (!commands)
			throw std::runtime_error("Console Settings has no command allocation to verify");
		if ((frames == 34 || frames == 26) && !phoenix::invoke_navigation(app, "console/1/game.control/61776179"))
			throw std::runtime_error("Console Settings Away/resume action refused");
		if (frames == 18)
		{
			if (!phoenix::invoke_navigation(app, "console/1/local-settings.close"))
				throw std::runtime_error("Could not close Console Settings");
			std::cout << "Console Settings complexity automatic/direct and return allocation passed\n";
		}
	}
	const auto readout = [&](const std::string &title)
	{
		return std::any_of(app.screen_readouts.begin(), app.screen_readouts.end(),
		                   [&](const auto &row)
		                   {
			                   return row.second.find(title) != std::string::npos &&
			                          row.second.find("12750") != std::string::npos;
		                   });
	};
	if (frames == 16 && !phoenix::invoke_navigation(app, "console/1/messages"))
		throw std::runtime_error("Could not open Console Message History");
	if (frames == 14 && !phoenix::probe_console_report(1))
		return pending("Controller report was not available in Message History");
	if (frames == 12)
	{
		if (!readout("Engine Console report"))
			throw std::runtime_error("AI officer report missing from Console semantics");
		TakeScreenshot("controller-report-console.png");
		app.screen = phoenix::Screen::viewscreen;
	}
	if (frames == 1)
	{
		if (!readout("Engine public report") || readout("Engine Console report"))
			throw std::runtime_error("Viewscreen report missing or private addressed report leaked");
		std::cout << "AI officer Console report and explicit emergency Viewscreen report smoke passed\n";
	}
	return true;
}
