// Explicit production-content graphics evidence; excluded from automatic gates.
template <typename Frames> void courier_radar_coverage_smoke(Application &app, const std::string &prefix, Frames frames)
{
	SetWindowSize(1920, 1080);
	SetTargetFPS(0);
	for (int stars = 1; stars <= 3; ++stars)
	{
		require(app.session->command({app.session->observation()->own.id, "preset", stars - 1, {}}),
		        "select Courier radar complexity");
		for (int tick = 0; tick < 60; ++tick)
			app.session->tick();
		frames();
		const auto view = app.session->observation();
		require(view->gameplay.at("combat").at("weapons").size() == 6,
		        "Courier has five offensive weapons and one PDC");
		for (const auto &arc : view->gameplay.at("combat").at("arcs"))
		{
			require(arc.at("kind") == "weapon", "Courier projection omits duplicate mount outlines");
			if (arc.at("id") == "top-gun" || arc.at("id") == "bottom-gun")
				require(arc.at("arc_mdeg") == 360000, "Courier gun coverage is full traverse");
		}
		ConsoleModel model;
		model.refresh(app.session);
		bool semantic_coverage = false;
		for (const auto &[id, label] : model.gameplay_readouts())
			if (id.starts_with("arc."))
			{
				semantic_coverage |= label.find("Top Gun") != std::string::npos &&
				                     label.find("Bottom Gun") != std::string::npos &&
				                     label.find("360") != std::string::npos;
				std::cout << "Radar semantic " << stars << " " << id << ": " << label << '\n';
			}
		require(semantic_coverage, "accessible radar description names both full-traverse guns");
		TakeScreenshot((prefix + "-stars-" + std::to_string(stars) + ".png").c_str());
	}
	const auto set = [&](const std::string &equipment, const std::string &action, int value)
	{
		const auto root = app.session->observation();
		std::vector<const Observation *> sources{&*root};
		for (const auto &extra : root->additional_consoles)
			sources.push_back(&extra);
		for (const auto *source : sources)
			for (const auto &controller : source->gameplay.at("controllers"))
				for (const auto &[channel, state] : controller.at("commands").items())
					if (state.at("action") == action &&
					    std::find(state.at("consumers").begin(), state.at("consumers").end(), equipment) !=
					        state.at("consumers").end())
					{
						OperatingCommand command{root->own.id,
						                         {},
						                         value,
						                         {},
						                         controller.at("id"),
						                         channel,
						                         controller.at("generation").get<std::uint64_t>()};
						command.console = source->console;
						require(app.session->command(command),
						        "manual coverage probe uses a permitted Controller command");
						app.session->tick();
						frames();
						return;
					}
		throw std::runtime_error("Missing coverage probe command: " + equipment + " / " + action);
	};
	set("torpedo-1", "magazine_select", 0);
	set("torpedo-2", "magazine_select", 1);
	set("pdc", "system_enabled", 0);
	bool hidden_tube = true, visible_tube = false, hidden_pdc = true;
	const auto mixed = app.session->observation();
	for (const auto &arc : mixed->gameplay.at("combat").at("arcs"))
	{
		hidden_tube &= arc.at("id") != "torpedo-1";
		visible_tube |= arc.at("id") == "torpedo-2";
		hidden_pdc &= arc.at("id") != "pdc";
	}
	require(hidden_tube && visible_tube && hidden_pdc, "mixed ammunition and disabled PDC have the intended coverage");
	TakeScreenshot((prefix + "-mixed-pdc-off.png").c_str());
	set("pdc", "system_enabled", 1);
	TakeScreenshot((prefix + "-mixed-pdc-on.png").c_str());
	for (const auto heading : {90000, 180000})
	{
		const auto view = app.session->observation();
		bool admitted = false;
		for (const auto &controller : view->gameplay.at("controllers"))
			if (controller.at("id") == "helm")
				admitted = app.session->command({view->own.id,
				                                 {},
				                                 heading,
				                                 {},
				                                 "helm",
				                                 "heading_mdeg",
				                                 controller.at("generation").get<std::uint64_t>()});
		require(admitted, "rotate the Courier through its ordinary heading Controller");
		for (int tick = 0; tick < 300; ++tick)
			app.session->tick();
		frames();
		std::cout << "Radar heading " << app.session->observation()->own.heading << '\n';
		TakeScreenshot((prefix + "-heading-" + std::to_string(heading / 1000) + ".png").c_str());
	}
	std::cout << "Courier radar coverage graphics passed\n";
}
