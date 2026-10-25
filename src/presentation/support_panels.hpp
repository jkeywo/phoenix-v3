// Compose the granted Support workload without changing Console command ownership.
std::string support_root_, support_comms_, support_navigation_, support_page_ = "support";
bool support_detail_ = false;
std::map<std::string, std::unique_ptr<ConsoleModel>> support_models_;
std::string support_popup_, support_layout_signature_;
bool support_layout_requested_ = false;
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
	bool panel_layout = false;
	if (snapshot)
		for (const auto &spec : resolve_consoles(app.session->catalog(), snapshot->own.definition))
			if (spec.id == snapshot->console)
			{
				const auto &definition = app.session->catalog().consoles.at(spec.definition);
				const auto signature = std::to_string(app.session->generation()) + definition.dump();
				if (signature != support_layout_signature_)
				{
					support_layout_signature_ = signature;
					support_layout_requested_ = false;
					if (definition.contains("layout"))
					{
						const auto program = compile_ui_layout(app.session->catalog(), definition);
						std::function<void(const UiNode &)> visit = [&](const UiNode &node)
						{
							support_layout_requested_ |= node.component == "power" || node.component == "repair";
							for (const auto &child : node.children)
								visit(child);
						};
						visit(program.root);
					}
				}
				panel_layout = support_layout_requested_;
			}
	std::string root, comms, navigation;
	std::set<std::string> auxiliaries;
	if (snapshot && panel_layout)
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
	if (snapshot && panel_layout)
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
	if (!support_active() && !station_component_active("power") && !station_component_active("repair"))
		return false;
	if (action == "support-page/repair")
		return authored_action("station-page/repair");
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
	if (action == "support-details/medical" && station_component_active("power"))
	{
		model_.act("game.teams-close");
		bool found = false;
		for (const auto &team : model_.observation()->gameplay.value("teams", Json::array()))
			if (!team.value("repair", false) && !team.at("orders").empty())
			{
				found = true;
				if (team.value("manual", false) && !team.value("reserved", false))
				{
					model_.act("game.team-order/" + team.at("id").get<std::string>() + "/" +
					           team.at("orders")[0].at("id").get<std::string>());
					break;
				}
			}
		if (!found)
			return false;
		model_.act("game.status-close");
		support_popup_ = "medical";
		systems_open_ = true;
		return true;
	}
	if (action.starts_with("support-details/"))
	{
		const auto detail = action.substr(16);
		if (!station_component_active("repair") || !detail.starts_with("team/"))
			return false;
		const auto teams = model_.observation()->gameplay.value("teams", Json::array());
		if (std::none_of(teams.begin(), teams.end(),
		                 [&](const Json &team)
		                 {
			                 return team.at("id") == detail.substr(5) && team.value("repair", false);
		                 }))
			return false;
		model_.act("game.status-close");
		support_popup_ = detail;
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
