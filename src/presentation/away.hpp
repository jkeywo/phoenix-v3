std::string away_notice_;
std::vector<std::pair<std::string, std::string>> away_readouts_;
bool away_attention() const
{
	for (const auto &m : model_.away_missions())
		for (const auto &s : m.at("slots"))
			if (s.at("assigned") == "" && !s.at("choices").empty())
				return true;
	return false;
}
std::string away_markup()
{
	away_readouts_.clear();
	auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto line = [&](const std::string &s)
	{
		const auto key = id("away/readout/" + std::to_string(away_readouts_.size()));
		away_readouts_.emplace_back(key, s);
		return "<p id=\"" + key + "\">" + esc(s) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Away missions")) + "</h2>" + line(localise(away_notice_));
	const auto missions = model_.away_missions();
	if (missions.empty())
		html += line(localise("No available away missions"));
	for (const auto &m : missions)
	{
		auto label = [&](const std::string &asset)
		{
			return localise(m.at("labels").value(asset, asset));
		};
		const auto key = m.at("id").get<std::string>();
		const auto phase = m.at("phase").get<std::string>();
		html += "<div class=\"personnel-row\"><h2>" + esc(localise(m.at("title"))) + "</h2>";
		if (m.at("result") != "")
		{
			const auto result = m.at("result").get<std::string>();
			html += line(localise(result.starts_with("complete:") ? "Mission completed" : "Mission failed"));
		}
		html += line(localise(phase) + (m.at("blocker") == "" ? "" : " / " + localise(m.at("blocker"))));
		for (const auto &o : m.at("accompanying"))
			html += line(localise("Accompanying officer") + ": " + label("officer:" + o.get<std::string>()));
		html += line(localise("Transport") + ": " + m.at("transport").get<std::string>());
		auto button = [&](const std::string &action, const std::string &value, const std::string &label)
		{
			return settings_button("away/" + action + "/" + key + "/" + value, label);
		};
		if (m.at("dispatch").get<bool>())
		{
			if (phase == "available")
				html += button("open", "", "Open assignments");
			if ((phase == "open" || phase == "recovered") && m.at("result") == "")
				html += button("send", "", "Send");
			if (phase == "deployed")
				html += button("recall", "", "Recall");
			if (phase == "outbound" || phase == "inbound")
				html += button("cancel", "", "Cancel transfer");
		}
		if (phase == "open" || phase == "recovered")
			for (const auto &t : m.at("transports"))
				html += button("transport", t, localise("Assign transport") + ": " + t.get<std::string>());
		for (const auto &s : m.at("slots"))
		{
			const auto slot = s.at("id").get<std::string>();
			html += line(slot + " (" + localise(s.at("required").get<bool>() ? "Required" : "Optional") +
			             "): " + label(s.at("assigned").get<std::string>()));
			if ((phase == "open" || phase == "recovered") && s.at("owned").get<bool>())
				html += button("withdraw", slot, "Withdraw assignment");
			if (s.at("assigned") == "")
				for (const auto &c : s.at("choices"))
					html += button("assign", slot + "/" + c.get<std::string>(),
					               localise("Assign") + ": " + label(c.get<std::string>()));
		}
		html += "</div>";
	}
	return html;
}
bool away_action(const std::string &action)
{
	if (action == "away")
	{
		settings_open_ = true;
		local_settings_tab_ = "away";
		cancel_pointer();
		model_.release_controls();
		document_->SetClass("local-settings-open", true);
		return true;
	}
	if (!settings_open_ || local_settings_tab_ != "away" || !action.starts_with("away/"))
		return false;
	const auto a = action.find('/', 5), b = action.find('/', a + 1);
	if (a == std::string::npos || b == std::string::npos)
		return false;
	away_notice_ = model_.away_action(action.substr(5, a - 5), action.substr(a + 1, b - a - 1), action.substr(b + 1))
	                   ? "Mission command accepted"
	                   : "Mission command refused; check readiness and ownership";
	return true;
}
