// Compose the granted Support workload without changing Console command ownership.
std::string support_root_, support_comms_, support_navigation_, support_page_ = "support";
bool support_detail_ = false;
std::map<std::string, std::unique_ptr<ConsoleModel>> support_models_;
std::string support_popup_;
bool support_active() const
{
	return !support_root_.empty();
}
bool support_primary() const
{
	return support_active() && model_.observation() && model_.observation()->console == support_root_;
}
void refresh_support_panels(Application &app)
{
	if (!app.session)
	{
		support_root_.clear();
		support_models_.clear();
		return;
	}
	const auto snapshot = app.session->console_snapshot(route_);
	bool generator = false, repair = false, panel_layout = false;
	if (snapshot)
		for (const auto &system : snapshot->systems)
		{
			generator |= system.kind == OperatingSystem::Generator;
			repair |= system.kind == OperatingSystem::Team;
		}
	if (snapshot)
		for (const auto &spec : resolve_consoles(app.session->catalog(), snapshot->own.definition))
			if (spec.id == snapshot->console)
				panel_layout = app.session->catalog().consoles.at(spec.definition).at("widgets").empty();
	std::string root, comms, navigation;
	std::set<std::string> auxiliaries;
	if (snapshot && generator && repair && panel_layout)
		for (const auto &spec : resolve_consoles(app.session->catalog(), snapshot->own.definition))
		{
			if (spec.id == snapshot->console)
				continue;
			const auto found = std::find_if(snapshot->additional_consoles.begin(), snapshot->additional_consoles.end(),
			                                [&](const auto &extra)
			                                {
				                                return extra.console == spec.id;
			                                });
			if (found == snapshot->additional_consoles.end())
				continue;
			for (const auto &widget : app.session->catalog().consoles.at(spec.definition).at("widgets"))
			{
				if (widget.at("kind") == "conversations")
					comms = spec.id;
				if (widget.at("kind") == "navigation")
					navigation = spec.id;
			}
			if (!found->gameplay.value("ftl", Json::array()).empty())
				auxiliaries.insert(spec.id);
		}
	if (!comms.empty())
		root = snapshot->console;
	if (root != support_root_)
	{
		support_page_ = "support";
		support_detail_ = false;
		support_popup_.clear();
		support_models_.clear();
	}
	support_root_ = root;
	support_comms_ = comms;
	support_navigation_ = navigation;
	if (!support_active())
		return;
	for (const auto &console : auxiliaries)
	{
		auto &source = support_models_[console];
		if (!source)
			source = std::make_unique<ConsoleModel>();
		source->refresh(app.session, route_);
		if (!source->observation() || source->observation()->console != console)
		{
			source->act("game.console/" + console);
			source->refresh(app.session, route_);
		}
	}
	std::erase_if(support_models_,
	              [&](const auto &entry)
	              {
		              return !auxiliaries.contains(entry.first);
	              });
}
bool support_action(const std::string &action)
{
	if (!support_active())
		return false;
	if (action.starts_with("support-page/") || action == "support-close-details")
	{
		const auto page = action == "support-close-details" ? std::string("support") : action.substr(13);
		if (page != "support" && page != "repair")
			return false;
		if (!model_.act("game.console/" + support_root_))
			return false;
		model_.refresh(app_->session, route_);
		cancel_pointer();
		support_page_ = page;
		support_detail_ = false;
		if (page == "support")
			model_.act("game.teams-close");
		settings_open_ = false;
		document_->SetClass("local-settings-open", false);
		document_->SetClass("station-page-open", false);
		return true;
	}
	if (action == "support-navigation" && !support_navigation_.empty())
	{
		support_detail_ = model_.act("game.console/" + support_navigation_);
		return support_detail_;
	}
	if (action.starts_with("support-details/"))
	{
		model_.act("game.status-close");
		support_popup_ = action.substr(16);
		systems_open_ = true;
		return true;
	}
	if (action.starts_with("support-console/"))
	{
		const auto split = action.find('/', 16);
		if (split == std::string::npos)
			return false;
		const auto source = support_models_.find(action.substr(16, split - 16));
		return source != support_models_.end() && source->second->act(action.substr(split + 1));
	}
	return false;
}
bool support_repair_control(const std::string &name, const Json &descriptor) const
{
	if (name.starts_with("game.team") || name.starts_with("game.compartment/"))
		return true;
	if (descriptor.value("controller", "").find("repair") != std::string::npos)
		return true;
	const auto consumers = command_consumers(*model_.observation(), descriptor);
	for (const auto &system : model_.observation()->systems)
		if (system.kind == OperatingSystem::Team &&
		    std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
			return true;
	return false;
}
void support_controls(std::vector<std::pair<std::string, std::string>> &controls)
{
	if (!support_primary())
		return;
	const auto descriptors = model_.observation()->gameplay.value("controls", Json::array());
	if (support_page_ == "repair")
	{
		std::erase_if(controls,
		              [](const auto &control)
		              {
			              return control.first != "game.teams-close";
		              });
		controls.emplace_back("support-details/repair", localise("Repair priorities and teams"));
		return;
	}
	std::erase_if(controls,
	              [&](const auto &control)
	              {
		              Json descriptor = Json::object();
		              for (const auto &entry : descriptors)
			              if (descriptor_action(entry.at("id")) == control.first)
			              {
				              descriptor = entry;
				              break;
			              }
		              const bool repair = support_repair_control(control.first, descriptor);
		              if (support_page_ == "repair")
			              return !repair;
		              if (repair || descriptor.empty())
			              return true;
		              const auto action = descriptor.value("action", "");
		              return action != "system_enabled" && action != "intent_boolean" && action != "intent_integer" &&
		                     action != "generator_output";
	              });
	if (support_page_ == "support")
	{
		for (const auto &system : model_.observation()->systems)
			if (system.kind == OperatingSystem::Generator)
				controls.emplace_back("game.status/" + system.id, localise("Reactor details"));
		for (const auto &[console, source] : support_models_)
			for (const auto &[name, label] : source->gameplay_controls())
				if (name.starts_with("game.control/"))
					controls.emplace_back("support-console/" + console + "/" + name, label);
		if (!support_navigation_.empty())
			controls.emplace_back("support-navigation", localise("Navigation details"));
	}
	controls.emplace_back("support-details/" + support_page_, localise("Details"));
}
void support_cards(std::vector<EngineeringCard> &cards)
{
	if (!support_primary())
		return;
	if (support_page_ == "repair")
	{
		cards.clear();
		for (const auto &part :
		     model_.observation()->gameplay.value("combat", Json::object()).value("compartments", Json::array()))
		{
			EngineeringCard card{"repair/" + part.at("id").get<std::string>(), engineering_name(part.at("id")), {}};
			card.condition =
			    int(part.at("hp").get<std::int64_t>() * 100 / std::max<std::int64_t>(1, part.at("maximum")));
			card.summary = localise("Condition") + ": " + std::to_string(*card.condition) + "%";
			if (part.at("disabled").get<bool>())
				card.summary += " | " + localise("Disabled");
			cards.push_back(std::move(card));
		}
		return;
	}
	std::erase_if(cards,
	              [&](const auto &card)
	              {
		              if (support_page_ == "repair")
			              return true;
		              for (const auto &system : model_.observation()->systems)
			              if (system.id == card.id)
				              return system.kind != OperatingSystem::Generator;
		              return true;
	              });
	for (auto &card : cards)
	{
		card.summary.clear();
		if (card.heat)
			card.summary += localise("Heat") + ": " + std::to_string(*card.heat) + "%";
		if (card.battery)
			card.summary += " | " + localise("Battery") + ": " + std::to_string(*card.battery) + "%";
		for (const auto &system : model_.observation()->systems)
			if (system.id == card.id && system.telemetry.contains("actual_output"))
				card.summary +=
				    " | " + localise("Output") + ": " + std::to_string(system.telemetry.at("actual_output")) + " / s";
		card.controls.emplace_back("game.status/" + card.id, localise("Details"));
	}
}
void support_readouts(std::vector<std::pair<std::string, std::string>> &readouts)
{
	if (!support_primary())
		return;
	readouts.clear();
	const auto &view = *model_.observation();
	const auto combat = view.gameplay.value("combat", Json::object());
	if (support_page_ == "repair")
	{
		const auto compartments = combat.value("compartments", Json::array());
		const auto damaged = std::count_if(compartments.begin(), compartments.end(),
		                                   [](const auto &part)
		                                   {
			                                   return part.at("hp") < part.at("maximum");
		                                   });
		readouts.emplace_back("support/repair/summary", localise("Damaged compartments") + ": " +
		                                                    std::to_string(damaged) + " / " +
		                                                    std::to_string(compartments.size()));
		const auto controls = model_.gameplay_controls();
		for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
			if (descriptor.value("controller", "").find("repair") != std::string::npos)
				for (const auto &controller : view.gameplay.value("controllers", Json::array()))
					if (controller.at("id") == descriptor.at("controller"))
					{
						const auto commands = controller.value("commands", Json::object());
						const auto channel = descriptor.value("channel", "");
						if (commands.contains(channel) && commands.at(channel).at("value") == descriptor.at("value"))
							for (const auto &[name, label] : controls)
								if (name == descriptor_action(descriptor.at("id")))
									readouts.emplace_back("support/repair/priority",
									                      localise("Repair priority") + ": " + engineering_name(label));
					}
		return;
	}
	for (const auto &shield : combat.value("shields", Json::array()))
		readouts.emplace_back("support/shield/" + shield.at("id").get<std::string>(),
		                      engineering_name(shield.at("id")) + ": " +
		                          std::to_string(shield.at("strength").get<std::int64_t>()) + " / " +
		                          std::to_string(shield.at("capacity").get<std::int64_t>()));
	for (const auto &[console, source] : support_models_)
		for (const auto &drive : source->observation()->gameplay.value("ftl", Json::array()))
			readouts.emplace_back("support/ftl/" + console,
			                      localise("FTL") + ": " +
			                          localise(drive.at("ready").get<bool>() ? "Ready to depart"
			                                                                 : drive.at("blocker").get<std::string>()) +
			                          " | " +
			                          std::to_string(drive.at("energy").get<std::int64_t>() * 100 /
			                                         std::max<std::int64_t>(1, drive.at("capacity"))) +
			                          "%");
}
void finish_support_panels(const ConsoleRegion &region)
{
	const bool detail = support_active() && support_detail_ && !settings_open_ && model_.observation() &&
	                    model_.observation()->console == support_navigation_;
	document_->SetClass("support-detail-open", detail);
	auto *close = document_->GetElementById(id("support-close-details"));
	if (!close)
	{
		auto created = document_->CreateElement("button");
		created->SetId(id("support-close-details"));
		close = document_->AppendChild(std::move(created));
		close->SetInnerRML(Rml::StringUtilities::EncodeRml(localise("Close details")));
		close->SetClass("support-detail-close", true);
	}
	property(close, "display", detail ? "block" : "none");
	auto *body = document_->GetElementById("authored-body");
	property(body, "left", detail ? "3%" : "0dp");
	property(body, "right", detail ? "3%" : "0dp");
	property(body, "bottom", detail ? "3%" : "0dp");
	property(body, "background-color", detail ? "#102027" : "transparent");
	property(body, "border", detail ? "2dp #988b71" : "0dp");
	if (detail)
	{
		property(body, "top", "156dp");
		context_->Update();
		app_->screen_controls.emplace_back(id("support-close-details"), localise("Close details"));
		place(*app_, region, id("support-close-details"));
	}
}
