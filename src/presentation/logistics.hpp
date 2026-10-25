std::string supply_notice_;
std::int64_t supply_quantity_ = 10;
std::vector<std::pair<std::string, std::string>> supply_readouts_;
std::string supply_line(const std::string &text)
{
	const auto key = id(station_component_scope_ + "/supply/readout/" + std::to_string(supply_readouts_.size()));
	supply_readouts_.emplace_back(key, text);
	return "<p id=\"" + key + "\">" + Rml::StringUtilities::EncodeRml(text) + "</p>";
}
std::string supply_button(const std::string &kind, const std::string &action, const std::string &system,
                          const std::string &text, const std::string &label)
{
	return settings_button("supply/" + kind + "/" + action + "/" + system + "/" + text, label);
}
std::string logistics_markup()
{
	supply_readouts_.clear();
	std::string html = "<h2>" + localise("Stores and deliveries") + "</h2>" + supply_line(localise(supply_notice_));
	const auto board = model_.logistics();
	const bool manual = board.value("manual", true);
	if (manual)
	{
		html += supply_line(localise("Transfer quantity") + ": " + std::to_string(supply_quantity_));
		html += settings_button("supply-quantity/down", "Reduce transfer quantity");
		html += settings_button("supply-quantity/up", "Increase transfer quantity");
	}
	for (const auto &s : board.value("stores", Json::array()))
	{
		bool controllable = false;
		if (model_.observation())
			for (const auto &ctrl : model_.observation()->gameplay.at("controllers"))
				controllable |= ctrl.at("id") == s.at("controller") && ctrl.value("human", false);
		const auto store = s.at("id").get<std::string>();
		const auto resource_label = [&](const std::string &id)
		{
			return localise(s.value("resource_labels", Json::object()).value(id, id));
		};
		html += supply_line(localise(store) + (s.at("working").get<bool>() ? "" : " / " + localise("Unavailable")));
		if (controllable && s.at("main").get<bool>())
			html += supply_button("logistics", s.at("held").get<bool>() ? "release" : "hold", store, "",
			                      s.at("held").get<bool>() ? "Release stock" : "Hold stock");
		if (s.value("single_resource", false))
		{
			html += supply_line(localise("Selected resource") + ": " +
			                    resource_label(s.value("selected_resource", std::string{})));
			for (const auto &resource : s.value("resource_choices", Json::array()))
			{
				const auto choice = resource.get<std::string>();
				const auto choices = s.value("selectable_resources", Json::array());
				if (manual && controllable && s.value("selected_resource", std::string{}) != choice &&
				    std::find(choices.begin(), choices.end(), resource) != choices.end())
					html += supply_button("logistics", "select", store, choice,
					                      localise("Select resource") + ": " + resource_label(choice));
				else
					html += supply_line(resource_label(choice));
			}
		}
		for (const auto &[m, q] : s.at("quantities").items())
		{
			const auto target = s.at("targets").value(m, std::int64_t{0});
			html +=
			    supply_line(localise(m) + ": " + q.dump() + " / " + localise("Target") + " " + std::to_string(target) +
			                " / " + localise("Receiving space") + " " + s.at("space").at(m).dump());
			if (controllable)
				html += supply_button("logistics", "target", store,
				                      m + "/" + std::to_string(std::max<std::int64_t>(0, target - 1)),
				                      localise("Reduce target") + " " + localise(m));
			if (controllable)
				html += supply_button("logistics", "target", store, m + "/" + std::to_string(target + 1),
				                      localise("Increase target") + " " + localise(m));
		}
	}
	if (model_.observation())
	{
		const auto &gameplay = model_.observation()->gameplay;
		const auto controllable = [&](const std::string &equipment)
		{
			for (const auto &controller : gameplay.value("controllers", Json::array()))
				if (controller.value("human", false))
					for (const auto &channel : controller.at("commands"))
						for (const auto &consumer : channel.at("consumers"))
							if (consumer == equipment)
								return true;
			return false;
		};
		for (const auto &magazine : gameplay.value("combat", Json::object()).value("magazines", Json::array()))
		{
			const auto system = magazine.at("id").get<std::string>();
			const auto resource_label = [&](const std::string &id)
			{
				return localise(magazine.value("resource_labels", Json::object()).value(id, id));
			};
			html += supply_line(localise(system) + " / " + localise("Selected resource") + ": " +
			                    resource_label(magazine.at("selected_resource")));
			const auto selectable = magazine.value("selectable_resources", Json::array());
			for (const auto &resource : magazine.at("resource_choices"))
			{
				const auto choice = resource.get<std::string>();
				if (magazine.value("manual", false) && controllable(system) &&
				    resource != magazine.at("selected_resource") &&
				    std::find(selectable.begin(), selectable.end(), resource) != selectable.end())
					html += supply_button("magazine", "select", system, choice,
					                      localise("Select resource") + ": " + resource_label(choice));
				else
					html += supply_line(resource_label(choice));
			}
		}
		for (const auto &processor : gameplay.value("processors", Json::array()))
		{
			const auto system = processor.at("id").get<std::string>();
			std::string selected_label = processor.at("recipe");
			for (const auto &recipe : processor.at("recipes"))
				if (recipe.at("id") == processor.at("recipe"))
					selected_label = recipe.at("label");
			html += supply_line(
			    localise(system) + " / " + localise("Selected recipe") + ": " + localise(selected_label) + " / " +
			    localise(processor.at("active").get<bool>() ? "Processing" : "Idle") + " / " + localise("Progress") +
			    ": " + processor.at("progress").dump() + "/" + processor.at("work").dump());
			for (const auto &recipe : processor.at("recipes"))
			{
				const auto choice = recipe.at("id").get<std::string>();
				const auto label = localise(recipe.at("label"));
				if (processor.value("manual", false) && controllable(processor.at("equipment")) &&
				    choice != processor.at("recipe").get<std::string>())
					html +=
					    supply_button("processor", "select", system, choice, localise("Select recipe") + ": " + label);
				else
					html += supply_line(label);
			}
		}
	}
	for (const auto &r : board.value("requests", Json::array()))
	{
		html += supply_line(localise("Waiting delivery") + " #" + r.at("id").dump() + ": " +
		                    r.at("material").get<std::string>() + " " + r.at("remaining").dump());
		if (manual)
			html += supply_button("logistics", "cancel", r.at("id").dump(), "", "Cancel waiting delivery");
	}
	for (const auto &b : board.value("batches", Json::array()))
	{
		html += supply_line(localise("In transit") + " #" + b.at("id").dump() + ": " +
		                    b.at("material").get<std::string>() + " " + b.at("quantity").dump() + " / " +
		                    localise("Location") + " " + b.at("location").dump() + ":" + b.at("compartment").dump());
		if (manual)
			html += supply_button("logistics", "cancel", "batch:" + b.at("id").dump(), "", "Return delivery");
	}
	bool engineering_control = false;
	if (model_.observation())
		for (const auto &ctrl : model_.observation()->gameplay.at("controllers"))
			engineering_control |= ctrl.at("id") == board.value("controller", Json("")) && ctrl.value("human", false);
	bool route_control = false;
	if (model_.observation())
		for (const auto &ctrl : model_.observation()->gameplay.at("controllers"))
			route_control |= ctrl.at("id") == board.value("route_controller", Json("")) && ctrl.value("human", false);
	for (const auto &route : board.value("routes", Json::array()))
	{
		const auto from = route.at("from").get<std::string>(), to = route.at("to").get<std::string>();
		const bool closed = route.at("closed").get<bool>();
		html += supply_line(localise(from) + " → " + localise(to) + " / " + localise(closed ? "Closed" : "Open"));
		if (route_control)
			html += supply_button("logistics", "route", from, to + (closed ? "/open" : "/close"),
			                      localise(closed ? "Open route" : "Close route") + ": " + localise(from) + " → " +
			                          localise(to));
	}
	for (const auto &craft : board.value("cargo_craft", Json::array()))
	{
		html += supply_line(localise("Supply shuttle") + " #" + craft.at("id").dump() + " / " +
		                    localise(craft.at("authorised").get<bool>() ? "Authorised" : "Permission required"));
		if (craft.at("controllable").get<bool>())
			html += supply_button(
			    "logistics", "craft_allow", craft.at("id").dump(), craft.at("authorised").get<bool>() ? "0" : "1",
			    craft.at("authorised").get<bool>() ? "Revoke supply permission" : "Authorise cargo sorties");
	}
	for (const auto &offer : board.value("offers", Json::array()))
		if (manual)
			for (const auto &store : board.value("stores", Json::array()))
			{
				const auto material = offer.at("material").get<std::string>(),
				           source = store.at("id").get<std::string>();
				if (!store.at("quantities").contains(material) ||
				    store.at("quantities").at(material).get<std::int64_t>() < 1)
					continue;
				const auto destination =
				    offer.at("entity").dump() + "/" + offer.at("store").get<std::string>() + "/" + material + "/" +
				    std::to_string(std::min(supply_quantity_, store.at("quantities").at(material).get<std::int64_t>()));
				if (offer.at("connected").get<bool>())
					html += supply_button("logistics", "transfer", source, destination,
					                      localise("Send supply") + ": " + localise(material) + " → #" +
					                          offer.at("entity").dump());
				for (const auto &craft : board.value("cargo_craft", Json::array()))
					if (craft.at("authorised").get<bool>() && craft.at("available").get<bool>())
						for (const auto &cargo : craft.at("stores"))
						{
							bool accepts = false;
							for (const auto &type : cargo.at("accepts"))
								accepts |= type == material;
							if (!accepts)
								continue;
							const auto text =
							    craft.at("id").dump() + "/" + cargo.at("id").get<std::string>() + "/" + destination;
							html += supply_button("logistics", "deliver", source, text,
							                      localise("Deliver by shuttle") + ": " + localise(material) + " → #" +
							                          offer.at("entity").dump());
						}
			}
	for (const auto &job : board.value("cargo", Json::array()))
	{
		html += supply_line(localise("Cargo delivery") + " #" + job.at("id").dump() + ": " + localise(job.at("phase")) +
		                    " / " + localise("Report tick") + " " + job.at("report_tick").dump());
		if (job.at("phase") != "complete" && job.at("phase") != "lost")
			html += supply_button("logistics", "cargo_cancel", job.at("id").dump(), "", "Cancel cargo delivery");
	}

	return html;
}
std::string docking_markup()
{
	supply_readouts_.clear();
	std::string html = "<h2>" + localise("Docking") + "</h2>" + supply_line(localise(supply_notice_));
	const auto board = model_.docking();
	for (const auto &p : board.value("ports", Json::array()))
	{
		const auto port = p.at("id").get<std::string>();
		const auto phase = p.at("phase").get<std::string>();
		html += supply_line(localise(port) + " / " + localise(phase) +
		                    (p.at("working").get<bool>() ? "" : " / " + localise("Unavailable")));
		html += supply_button("dock", p.at("accepting").get<bool>() ? "reject" : "accept", port, "0",
		                      p.at("accepting").get<bool>() ? "Close allied docking" : "Allow allied docking");
		html += supply_button("dock", "supply", port, p.at("supply").get<bool>() ? "0" : "1",
		                      p.at("supply").get<bool>() ? "Stop supply service" : "Authorise supply service");
		if (phase != "free")
		{
			html += supply_line(localise("Connected entity") + ": " + p.at("target").dump());
			html += supply_button("dock", phase == "approach" ? "cancel" : "undock", port, "",
			                      phase == "approach" ? "Cancel approach" : "Undock");
			if (p.at("emergency").get<bool>())
				html += supply_button("dock", "emergency", port, "", "Emergency release");
		}
		else
			for (const auto &t : board.value("targets", Json::array()))
				if (t.at("compatibility") == p.at("compatibility") && t.at("available").get<bool>())
				{
					const auto target = t.at("entity").dump() + "/" + t.at("port").get<std::string>();
					html += supply_button("dock", "assist", port, target,
					                      localise("Assisted approach") + " #" + t.at("entity").dump() + " / " +
					                          t.at("port").get<std::string>());
					html += supply_button("dock", "connect", port, target,
					                      localise("Dock manually") + " #" + t.at("entity").dump());
				}
	}
	return html;
}
bool supply_action(const std::string &action)
{
	if (station_component_active("operations") && action.starts_with("supply-quantity/"))
	{
		supply_quantity_ = std::clamp<std::int64_t>(supply_quantity_ + (action.ends_with("up") ? 10 : -10), 1, 1000000);
		return true;
	}
	if ((!station_component_active("operations") && !station_component_active("docking")) ||
	    !action.starts_with("supply/"))
		return false;
	const auto a = action.find('/', 7), b = action.find('/', a + 1), c = action.find('/', b + 1);
	if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
		return false;
	const auto kind = action.substr(7, a - 7), verb = action.substr(a + 1, b - a - 1),
	           system = action.substr(b + 1, c - b - 1), text = action.substr(c + 1);
	if (!station_component_active(kind == "dock" ? "docking" : "operations"))
		return false;
	const bool ok = kind == "dock"                              ? model_.docking_action(verb, system, text)
	                : kind == "magazine" || kind == "processor" ? model_.selection_action(kind, system, text)
	                                                            : model_.logistics_action(verb, system, text);
	supply_notice_ = ok ? "Order accepted" : "Order refused; check readiness and ownership";
	return true;
}
