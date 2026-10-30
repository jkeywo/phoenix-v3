// Explicit graphical/route evidence against the production fit, never a content golden.
template <typename Frames> void courier_refinement_smoke(Application &app, const std::string &prefix, Frames frames)
{
	SetWindowSize(1920, 1080);
	SetTargetFPS(0);
	frames();
	const auto control = [&](const std::string &fragment)
	{
		for (const auto &[id, label] : app.screen_controls)
			if (id.find(fragment) != std::string::npos)
				return id;
		const auto alternate = fragment == "/throttle-impulse/" ? "/flight-impulse/"
		                       : fragment == "/all-stop/"       ? "/flight-stop/"
		                                                        : "/control-details/helm/drive.engine";
		for (const auto &[id, label] : app.screen_controls)
			if (id.find(alternate) != std::string::npos)
				return id;
		throw std::runtime_error("Missing refinement control: " + fragment);
	};
	const auto phases = [&]
	{
		std::vector<std::int64_t> result;
		const auto observed = app.session->observation();
		for (const auto &s : observed->systems)
			if (s.kind == OperatingSystem::Impulse && s.telemetry.contains("impulse_phase"))
				result.push_back(s.telemetry.at("impulse_phase"));
		return result;
	};
	const auto bearing_only = [&]
	{
		const auto view = app.session->observation();
		require(std::none_of(view->contacts.begin(), view->contacts.end(),
		                     [](const auto &c)
		                     {
			                     return c.id == 1;
		                     }),
		        "Morrow remains outside positional sensing on the station route");
		require(std::any_of(view->bearing_contacts.begin(), view->bearing_contacts.end(),
		                    [](const auto &c)
		                    {
			                    return c.id == 1;
		                    }),
		        "Morrow remains a long-range bearing along the station route");
	};
	for (int tick = 0; tick < 20; ++tick)
		app.session->tick();
	frames();
	bearing_only();
	require(app.screen_action("console/1/radar-contact/1"),
	        "select Morrow's bearing through the accessible radar control");
	frames();
	TakeScreenshot((prefix + "-bearing.png").c_str());
	for (const auto &[name, value] : app.screen_readouts)
		if (name.find("target-info") != std::string::npos)
		{
			const auto bounds = app.control_placements.at(name);
			std::cout << "Target readout " << value << " at " << bounds.x << "," << bounds.y << " " << bounds.width
			          << "x" << bounds.height << '\n';
		}
	const auto details = control("/throttle-details/");
	const auto bounds = app.control_placements.at(details);
	require(std::abs(bounds.width - bounds.height) < 1, "Details has a square touch target for its circular button");
	require(app.screen_action(details), "open throttle details");
	frames();
	TakeScreenshot((prefix + "-throttle-details.png").c_str());
	require(app.screen_action("console/1/systems-close"), "close throttle details");
	frames();
	const auto impulse = control("/throttle-impulse/");
	require(app.screen_action(impulse), "start the fitted impulse mechanisms");
	app.session->tick();
	frames();
	const auto charging = phases();
	require(!charging.empty() && std::all_of(charging.begin(), charging.end(),
	                                         [](auto p)
	                                         {
		                                         return p == 1;
	                                         }),
	        "all fitted impulse mechanisms charge together");
	TakeScreenshot((prefix + "-impulse-charging.png").c_str());
	for (int tick = 0; tick < 65; ++tick)
		app.session->tick();
	frames();
	const auto active = phases();
	require(std::all_of(active.begin(), active.end(),
	                    [](auto p)
	                    {
		                    return p == 2;
	                    }),
	        "paid impulse charging reaches transit");
	TakeScreenshot((prefix + "-impulse-active.png").c_str());
	require(app.screen_action(impulse), "cancel impulse through the same button");
	app.session->tick();
	frames();
	const auto idle = phases();
	require(std::all_of(idle.begin(), idle.end(),
	                    [](auto p)
	                    {
		                    return p == 0;
	                    }),
	        "all impulse mechanisms cancel together");
	require(app.screen_action(impulse), "restart impulse charging");
	app.session->tick();
	frames();
	require(app.screen_action(control("/all-stop/")), "All stop also cancels impulse charging");
	app.session->tick();
	frames();
	const auto stopped = phases();
	require(std::all_of(stopped.begin(), stopped.end(),
	                    [](auto p)
	                    {
		                    return p == 0;
	                    }),
	        "All stop leaves every impulse mechanism idle");
	std::string speed;
	for (const auto &field : app.screen_inputs)
	{
		if (field.id.find("/speed/") != std::string::npos)
			speed = field.id;
	}
	ConsoleModel pilot;
	pilot.refresh(app.session, 1);
	require(pilot.set_heading_degrees(0) &&
	            (speed.empty() ? pilot.set_speed_fraction(1) : app.set_control_value(speed, "100")),
	        "fly the station corridor");
	int ticks = 0;
	while (app.session->observation()->own.x < 1400000 && ticks++ < 12000)
	{
		app.session->tick();
		bearing_only();
	}
	require(ticks < 12000, "reach the starbase approach using physical flight");
	frames();
	TakeScreenshot((prefix + "-station-route.png").c_str());
	// Use the authored casualty position only to steer this manual evidence run.
	const auto &scene = app.session->catalog().maps.at("base.rescue-under-threat");
	const auto &casualty = scene.at("entities").front();
	bool detected = false;
	for (ticks = 0; ticks < 16000 && !detected; ++ticks)
	{
		if (ticks % 20 == 0)
		{
			const auto own = app.session->observation()->own;
			const auto angle = std::atan2(casualty.at("y_mm").template get<double>() - own.y,
			                              casualty.at("x_mm").template get<double>() - own.x) *
			                   180 / 3.141592653589793;
			require(pilot.set_heading_degrees(angle < 0 ? angle + 360 : angle), "steer the diversion toward Morrow");
		}
		app.session->tick();
		const auto view = app.session->observation();
		detected = std::any_of(view->contacts.begin(), view->contacts.end(),
		                       [](const auto &c)
		                       {
			                       return c.id == 1;
		                       });
	}
	require(detected, "diverting toward Morrow permits positional detection");
	frames();
	TakeScreenshot((prefix + "-diversion.png").c_str());
	std::cout
	    << "PASS bearing selection, circular Details, paid/coordinated impulse and station-route/diversion sensing\n";
}
