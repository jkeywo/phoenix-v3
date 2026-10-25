std::string personnel_selected_, personnel_notice_;
std::vector<std::pair<std::string, std::string>> personnel_readouts_;
std::string personnel_markup(Application &app)
{
	personnel_readouts_.clear();
	const auto board = model_.personnel();
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto paragraph = [&](const std::string &text, const std::string &identity)
	{
		const auto key = id("personnel/readout/" + identity);
		personnel_readouts_.emplace_back(key, text);
		return "<p id=\"" + key + "\">" + esc(text) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Personnel")) + "</h2><p>" +
	                   esc(localise("Select an officer, then a duty. Bonuses begin on arrival.")) + "</p>";
	if (!personnel_notice_.empty())
		html += "<p>" + esc(localise(personnel_notice_)) + "</p>";
	if (board.empty())
		return html;
	for (const auto &dep : board.at("departments"))
	{
		html += "<h2>" + esc(localise(dep.at("label"))) + "</h2>";
		const Json *selected = nullptr;
		for (const auto &o : board.at("officers"))
			if (o.at("department") == dep.at("id"))
			{
				if (o.at("id") == personnel_selected_)
					selected = &o;
				auto portrait = asset_path("../messages/console-1.png");
				const auto ref = o.at("portrait").get<std::string>();
				if (app.session->catalog().assets.contains(ref))
					portrait = register_menu_image(app.session->catalog().assets.at(ref));
				std::string label = (o.at("id") == personnel_selected_ ? localise("Selected") + ": " : std::string{}) +
				                    o.at("name").get<std::string>();
				label += " - " + localise(o.at("duty") == ""            ? "Reserve"
				                          : o.at("present").get<bool>() ? "On duty"
				                                                        : "Travelling");
				if (o.at("duty") != "")
					for (const auto &d : board.at("duties"))
						if (d.at("id") == o.at("duty"))
							label += ": " + localise(d.at("label"));
				if (o.contains("power"))
					label += " / " + localise(o.at("power"));
				label += " / " + o.at("location").get<std::string>();
				if (o.value("condition", std::string{"healthy"}) != "healthy")
					label += " / " + localise(o.at("condition")) +
					         (o.value("stabilised", false) ? " / " + localise("Stabilised") : "");
				if (o.at("remaining").get<std::int64_t>() > 0)
					label += " -> " + o.at("destination").get<std::string>();
				html += "<div class=\"personnel-row\"><img src=\"" + esc(portrait) + "\"/>" +
				        settings_button("personnel/select/" + o.at("id").get<std::string>(), label);
				for (const auto &t : o.at("traits"))
				{
					std::string description = localise(t.at("label"));
					for (const auto &c : t.at("contributions"))
						description += " / " + localise(c.at("property")) + " " +
						               std::to_string(c.at("delta_permille").get<std::int64_t>() / 10) + "%";
					html += paragraph(description, "officer/" + o.at("id").get<std::string>() + "/trait/" +
					                                   t.at("id").get<std::string>());
				}
				for (const auto &[action, label] :
				     model_.asset_away_controls("officer:" + o.at("id").get<std::string>()))
					html += settings_button(action, label);
				html += "</div>";
			}
		for (const auto &d : board.at("duties"))
			if (d.at("department") == dep.at("id"))
			{
				std::string label = localise(d.at("label")) + " / " + d.at("location").get<std::string>();
				bool occupied = false;
				for (const auto &o : board.at("officers"))
					if (o.at("duty") == d.at("id"))
					{
						occupied = true;
						label += " - " + o.at("name").get<std::string>();
					}
				if (!occupied)
					label += " - " + localise("No officer");
				html += paragraph(label, "duty/" + d.at("id").get<std::string>());
				for (const auto &effect : d.at("effects"))
					html += paragraph(effect.at("system").get<std::string>() + ": " + localise(effect.at("property")) +
					                      " " + std::to_string(effect.at("multiplier").get<std::int64_t>() / 10) + "%",
					                  "duty/" + d.at("id").get<std::string>() + "/effect/" +
					                      effect.at("system").get<std::string>() + "/" +
					                      effect.at("slot").get<std::string>() + "/" +
					                      effect.at("property").get<std::string>());
				if (board.value("manual", true) && selected &&
				    (selected->at("condition") == "healthy" || selected->at("condition") == "minor") &&
				    selected->at("duty") != d.at("id"))
				{
					std::string applicable;
					for (const auto &t : selected->at("traits"))
						for (const auto &effect : d.at("effects"))
						{
							const auto slot = effect.at("slot").get<std::string>();
							for (const auto &c : t.at("contributions"))
								if (c.at("role") == slot && c.at("property") == effect.at("property"))
									applicable += localise(t.at("label")) + ": " + localise(c.at("property")) + " " +
									              std::to_string(c.at("delta_permille").get<std::int64_t>() / 10) +
									              "%; ";
						}
					html += paragraph(applicable.empty() ? localise("No applicable bonus") : applicable,
					                  "duty/" + d.at("id").get<std::string>() + "/applicable");
					html += settings_button("personnel/assign/" + d.at("id").get<std::string>(),
					                        localise("Assign") + " " + selected->at("name").get<std::string>() +
					                            (occupied ? " (" + localise("relieve current officer") + ")" : ""));
					if (occupied && selected->at("duty") != "")
						html += settings_button("personnel/swap/" + d.at("id").get<std::string>(), "Swap duties");
				}
			}
		if (board.value("manual", true) && selected && selected->at("duty") != "")
			html += settings_button("personnel/relieve", "Relieve selected officer");
	}
	return html;
}
bool personnel_action(const std::string &action)
{
	if (action == "personnel")
		return authored_action("station-page/personnel");
	if (!station_component_active("personnel") || !action.starts_with("personnel/"))
		return false;
	if (action.starts_with("personnel/select/"))
	{
		personnel_selected_ = action.substr(17);
		personnel_notice_.clear();
		return true;
	}
	bool accepted = false;
	if (action.starts_with("personnel/assign/"))
		accepted = model_.personnel_action("assign", personnel_selected_, action.substr(17));
	if (action.starts_with("personnel/swap/"))
		accepted = model_.personnel_action("swap", personnel_selected_, action.substr(15));
	if (action == "personnel/relieve")
		accepted = model_.personnel_action("relieve", personnel_selected_);
	personnel_notice_ = accepted ? "Assignment requested" : "Assignment refused; refresh and try again";
	return true;
}
