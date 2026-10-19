// Explicitly invoked graphical smoke using the synthetic controller_ui.py packs.
// Run with --controller-report-smoke and the ordinary --game-pack/seat options.
inline void probe_controller_reports(phoenix::Application &app, int frames)
{
	if (frames == 52 || frames == 35)
	{
		const auto label = frames == 52 ? "Complexity: automatic" : "Complexity: direct";
		const auto choice = std::find_if(app.screen_controls.begin(), app.screen_controls.end(),
		                                 [&](const auto &row)
		                                 {
			                                 return row.second == label;
		                                 });
		if (choice == app.screen_controls.end())
			throw std::runtime_error("Console Settings complexity control missing");
		const auto id = choice->first;
		if (!phoenix::invoke_navigation(app, id))
			throw std::runtime_error("Console Settings complexity action refused");
	}
	if (frames == 40 || frames == 25)
	{
		const bool human = frames == 25;
		const auto observed = app.session->console_observation(1);
		if (!observed || observed->gameplay.at("profile") != (human ? "direct" : "automatic"))
			throw std::runtime_error("Console Settings complexity did not commit");
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("human") != human)
				throw std::runtime_error("Console Settings profile/return assigned the wrong operator");
		if (human)
			std::cout << "Console Settings complexity automatic/direct and return allocation passed\n";
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
	if (frames == 18 && !phoenix::probe_console_report(1))
		throw std::runtime_error("Controller report scroll failed");
	if (frames == 15)
	{
		if (!readout("Engine Console report"))
			throw std::runtime_error("Controller factual report missing from Console semantics");
		TakeScreenshot("controller-report-console.png");
		app.screen = phoenix::Screen::viewscreen;
	}
	if (frames == 1)
	{
		if (!readout("Engine public report") || readout("Engine Console report"))
			throw std::runtime_error("Viewscreen report missing or private addressed report leaked");
		std::cout << "Controller factual Console report and isolated public Viewscreen report smoke passed\n";
	}
}
