// Explicitly invoked graphical smoke using the synthetic controller_ui.py packs.
// Run with --controller-report-smoke and the ordinary --game-pack/seat options.
inline void probe_controller_reports(phoenix::Application &app, int frames)
{
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
