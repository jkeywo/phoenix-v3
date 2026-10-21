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
	auto paragraph = [&](const std::string &text)
	{
		const auto key = id("personnel/readout/" + std::to_string(personnel_readouts_.size()));
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
					for (const auto &[slot, b] : t.at("bonuses").items())
						description += " / " + localise(slot) + " " + std::to_string(b.get<std::int64_t>() / 10) + "%";
					html += paragraph(description);
				}
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
				html += paragraph(label);
				for (const auto &effect : d.at("effects"))
					html += paragraph(effect.at("system").get<std::string>() + ": " + localise(effect.at("slot")) +
					                  " " + std::to_string(effect.at("multiplier").get<std::int64_t>() / 10) + "%");
				if (selected && (selected->at("condition") == "healthy" || selected->at("condition") == "minor") &&
				    selected->at("duty") != d.at("id"))
				{
					std::string applicable;
					for (const auto &t : selected->at("traits"))
						for (const auto &effect : d.at("effects"))
						{
							const auto slot = effect.at("slot").get<std::string>();
							if (t.at("bonuses").contains(slot))
								applicable += localise(t.at("label")) + ": " + localise(slot) + " " +
								              std::to_string(t.at("bonuses").at(slot).get<std::int64_t>() / 10) + "%; ";
						}
					html += paragraph(applicable.empty() ? localise("No applicable bonus") : applicable);
					html += settings_button("personnel/assign/" + d.at("id").get<std::string>(),
					                        localise("Assign") + " " + selected->at("name").get<std::string>() +
					                            (occupied ? " (" + localise("relieve current officer") + ")" : ""));
					if (occupied && selected->at("duty") != "")
						html += settings_button("personnel/swap/" + d.at("id").get<std::string>(), "Swap duties");
				}
			}
		if (selected && selected->at("duty") != "")
			html += settings_button("personnel/relieve", "Relieve selected officer");
	}
	return html;
}
bool personnel_action(const std::string &action)
{
	if (action == "personnel")
	{
		settings_open_ = true;
		local_settings_tab_ = "personnel";
		cancel_pointer();
		model_.release_controls();
		document_->SetClass("local-settings-open", true);
		return true;
	}
	if (!settings_open_ || local_settings_tab_ != "personnel" || !action.starts_with("personnel/"))
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
