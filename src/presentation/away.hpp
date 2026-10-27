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
			if (m.contains("supply") && result.find(':') != std::string::npos)
				html += line(localise(result.substr(result.find(':') + 1)));
		}
		html += line(localise(phase) + (m.at("blocker") == "" ? "" : " / " + localise(m.at("blocker"))));
		for (const auto &o : m.at("accompanying"))
			html += line(localise("Accompanying officer") + ": " + label("officer:" + o.get<std::string>()));
		if (m.contains("boarding_check"))
		{
			const auto &c = m.at("boarding_check");
			const auto percent = [](std::int64_t value)
			{
				const auto magnitude = value < 0 ? -value : value;
				return std::string(value < 0 ? "-" : "") + std::to_string(magnitude / 10) +
				       (magnitude % 10 ? "." + std::to_string(magnitude % 10) : "") + "%";
			};
			html += line(localise("Encounter chance") + ": " + percent(c.at("chance_permille").get<int>()) + " / " +
			             localise("Defending teams") + ": " + std::to_string(c.at("defenders").get<int>()));
			for (const auto &[key, value] : c.at("factors").items())
				html += line(localise(key) + ": " + percent(value.get<std::int64_t>()));
		}
		html += line(localise("Transport") + ": " + label(m.at("transport").get<std::string>()));
		html += line(localise("Destination contact") + " #" + std::to_string(m.value("target", std::uint64_t{0})));
		if (m.value("ship_transport_reason", std::string{}) != "")
			html += line(localise(m.at("ship_transport_reason")));
		auto button = [&](const std::string &action, const std::string &value, const std::string &label)
		{
			return settings_button("away/" + action + "/" + key + "/" + value, label);
		};
		html += line(localise("Dispatch") + ": " + localise(m.value("owner", std::string{})));
		if (m.value("can_contribute", false))
			html += button("contribute", m.value("contributing", false) ? "0" : "1",
			               m.value("contributing", false) ? "Withdraw department contribution"
			                                              : "Authorise department contribution");
		if (m.contains("supply"))
		{
			const auto &s = m.at("supply");
			html += line(localise(s.at("material")) + ": " + std::to_string(s.at("quantity").get<std::int64_t>()) +
			             " / " + localise(s.at("phase")));
			html += line(localise("Payment") + ": " + localise(s.at("payment")) + " " +
			             std::to_string(s.at("payment_quantity").get<std::int64_t>()));
			html += line(localise("Delivered") + ": " + std::to_string(s.at("delivered").get<std::int64_t>()));
			if (s.at("manual").get<bool>())
				for (const auto &q : s.at("quantities"))
					html += button("quantity", std::to_string(q.get<std::int64_t>()),
					               q == 0 ? localise("Automatic manifest")
					                      : localise("Collect") + " " + std::to_string(q.get<std::int64_t>()));
		}
		for (const auto &s : m.value("craft_slots", Json::array()))
		{
			html += line(localise(s.at("id")) + ": " + label(s.at("assigned").get<std::string>()));
			if (s.value("lost", false))
				html += line(localise("Escort lost"));
			if (s.at("owned").get<bool>() && m.value("can_contribute", false))
				html += button("escort", s.at("id").get<std::string>() + "/", "Withdraw escort");
			for (const auto &choice : s.at("choices"))
				html += button("escort", s.at("id").get<std::string>() + "/" + choice.get<std::string>(),
				               localise("Assign escort") + ": " + label(choice.get<std::string>()));
		}
		if (m.value("transport_owned", false))
			html += button("transport", "", "Withdraw transport");
		if (m.at("dispatch").get<bool>())
		{
			if (phase == "available")
				html +=
				    button("open", "", m.value("automatic_dispatch", false) ? "Activate mission" : "Open assignments");
			if ((phase == "recovered" || (phase == "open" && !m.value("automatic_dispatch", false))) &&
			    m.at("result") == "")
				html += button("send", "", "Send");
			if (phase == "deployed")
				html += button("recall", "", "Recall");
			if (phase == "open" || phase == "outbound" || phase == "inbound")
				html += button("cancel", "", "Cancel transfer");
		}
		if ((phase == "open" || phase == "recovered" || phase == "deployed") &&
		    (m.at("result") == "" || phase == "deployed"))
			for (const auto &t : m.at("transports"))
				html += button("transport", t, localise("Assign transport") + ": " + label(t.get<std::string>()));
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
		return authored_action("station-page/away");
	if (!station_component_active("away") || !action.starts_with("away/"))
		return false;
	const auto a = action.find('/', 5), b = action.find('/', a + 1);
	if (a == std::string::npos || b == std::string::npos)
		return false;
	away_notice_ = model_.away_action(action.substr(5, a - 5), action.substr(a + 1, b - a - 1), action.substr(b + 1))
	                   ? "Mission command accepted"
	                   : "Mission command refused; check readiness and ownership";
	return true;
}
