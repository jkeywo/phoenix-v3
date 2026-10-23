std::string craft_notice_;
std::vector<std::pair<std::string, std::string>> craft_readouts_;
std::string craft_markup()
{
	craft_readouts_.clear();
	auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto line = [&](const std::string &s)
	{
		const auto key = id("craft/readout/" + std::to_string(craft_readouts_.size()));
		craft_readouts_.emplace_back(key, s);
		return "<p id=\"" + key + "\">" + esc(s) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Flight operations")) + "</h2>" + line(localise(craft_notice_));
	const auto rows = model_.craft();
	if (rows.empty())
		html += line(localise("No craft under this Console"));
	for (const auto &c : rows)
	{
		const auto craft = c.at("id").get<std::string>();
		html += "<div class=\"personnel-row\"><h2>" +
		        esc(c.at("bay").get<std::string>() + " / " + localise(c.at("role")) + " #" + craft) + "</h2>";
		html += line(localise(c.at("phase")) + " / " + localise(c.at("order")));
		if (c.value("blocker", std::string{}) != "")
			html += line(localise(c.at("blocker")));
		html += line(localise("Energy") + ": " + std::to_string(c.at("energy").get<std::int64_t>()) + "/" +
		             std::to_string(c.at("capacity").get<std::int64_t>()) + " / " + localise("Ammunition") + ": " +
		             std::to_string(c.at("ammunition").get<std::int64_t>()));
		html += line(localise("Condition") + ": " + std::to_string(c.at("condition").get<std::int64_t>()) + "/" +
		             std::to_string(c.at("maximum_condition").get<std::int64_t>()));
		if (!c.at("linked").get<bool>())
			html += line(localise("Link unavailable; showing last report") + " " +
			             std::to_string(c.at("report_tick").get<std::uint64_t>()));
		if (c.at("pending").get<bool>())
			html += line(localise("Order pending delivery"));
		auto button = [&](const std::string &action, const std::string &target, const std::string &label)
		{
			if (!c.value("manual", true))
				return std::string{};
			return settings_button("craft/" + action + "/" + craft + "/" + target, label);
		};
		if (c.at("phase") != "lost" && c.at("phase") != "consumed" &&
		    (c.at("role") != "boarding_torpedo" || c.at("phase") == "launching") &&
		    (c.at("phase") != "docked" || !c.at("available").get<bool>()))
			html += button("recall", "", "Recall");
		if (c.at("role") == "interceptor")
		{
			html += button("defend", "", "Defend carrier");
			if (model_.observation())
				html += button("escort", std::to_string(model_.observation()->own.id), "Escort own ship");
		}
		if (model_.observation())
			for (const auto &target : model_.observation()->contacts)
				if (target.id < 1000000000ULL && std::to_string(target.id) != craft)
				{
					const auto name = localise(target.definition) + " #" + std::to_string(target.id);
					if (c.at("role") == "interceptor")
						html += button("escort", std::to_string(target.id), localise("Escort") + ": " + name);
					if (c.at("role") == "bomber" && target.disposition == "hostile")
						html += button("bomb", std::to_string(target.id), localise("Bomb target") + ": " + name);
				}
		html += "</div>";
	}
	return html;
}
bool craft_action(const std::string &action)
{
	if (action == "craft")
	{
		settings_open_ = true;
		local_settings_tab_ = "craft";
		cancel_pointer();
		model_.release_controls();
		document_->SetClass("local-settings-open", true);
		return true;
	}
	if (!settings_open_ || local_settings_tab_ != "craft" || !action.starts_with("craft/"))
		return false;
	const auto a = action.find('/', 6), b = action.find('/', a + 1);
	if (a == std::string::npos || b == std::string::npos)
		return false;
	craft_notice_ = model_.craft_action(action.substr(6, a - 6), action.substr(a + 1, b - a - 1), action.substr(b + 1))
	                    ? "Flight order accepted"
	                    : "Flight order refused; check readiness and ownership";
	return true;
}
