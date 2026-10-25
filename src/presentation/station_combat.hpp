// A granted radar-only combat Console can share its operator's flight surface.
// Commands still pass through that Console's own model and ordinary admission.
ConsoleModel inline_model_;
std::string inline_console_;
bool systems_open_ = false;
std::vector<std::pair<std::string, std::string>> popup_controls_;

void refresh_inline_console(Application &app)
{
	inline_console_.clear();
	const auto *layout = model_.layout();
	if (!layout || !std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
	                            [](const auto &widget)
	                            {
		                            return widget.at("kind") == "throttle";
	                            }))
		return;
	const auto primary = app.session->console_snapshot(route_);
	if (!primary)
		return;
	for (const auto &spec : resolve_consoles(app.session->catalog(), primary->own.definition))
	{
		const auto &definition = app.session->catalog().consoles.at(spec.definition);
		const auto &widgets = definition.at("widgets");
		if (widgets.size() != 1 || widgets[0].at("kind") != "radar")
			continue;
		for (const auto &extra : primary->additional_consoles)
			if (extra.console == spec.id &&
			    !extra.gameplay.value("combat", Json::object()).value("weapons", Json::array()).empty())
			{
				inline_model_.refresh(app.session, route_);
				if (!inline_model_.observation() || inline_model_.observation()->console != extra.console)
				{
					inline_model_.act("game.console/" + extra.console);
					inline_model_.refresh(app.session, route_);
				}
				inline_console_ = extra.console;
				return;
			}
	}
}
std::vector<std::pair<std::string, std::string>> helm_weapon_controls()
{
	std::vector<std::pair<std::string, std::string>> result;
	if (inline_console_.empty() || !inline_model_.observation())
		return result;
	const auto &view = *inline_model_.observation();
	for (const auto &system : view.systems)
		if (system.kind == OperatingSystem::Weapon)
			for (const auto &control : view.gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "system_enabled" &&
				    control.value("value", -1) == (system.enabled ? 0 : 1))
				{
					const auto consumers = command_consumers(view, control);
					if (std::find(consumers.begin(), consumers.end(), system.id) == consumers.end())
						continue;
					result.emplace_back("inline/" + descriptor_action(control.at("id")),
					                    engineering_name(system.id) + ": " +
					                        localise(system.enabled ? "Enabled" : "Disabled"));
					break;
				}
	return result;
}
std::vector<std::pair<std::string, std::string>> helm_target_readouts()
{
	std::vector<std::pair<std::string, std::string>> result;
	auto &target_model = inline_console_.empty() ? model_ : inline_model_;
	const auto view = model_.observation();
	if (!view)
		return result;
	const auto selected = target_model.sensor_contact();
	result.emplace_back("target/title", localise("Selected target"));
	bool found = false;
	for (const auto &contact : view->contacts)
		if (contact.id == selected)
		{
			found = true;
			const auto &catalog = app_->session->catalog();
			const auto name = catalog.entities.contains(contact.definition)
			                      ? catalog.entities.at(contact.definition).value("name", contact.definition)
			                      : contact.definition;
			result.emplace_back("target/name", localise(name) + " #" + std::to_string(contact.id));
			result.emplace_back("target/range", localise("Range") + ": " +
			                                        std::to_string(static_cast<std::int64_t>(
			                                                           std::hypot(double(contact.x - view->own.x),
			                                                                      double(contact.y - view->own.y))) /
			                                                       1000) +
			                                        " m");
			result.emplace_back(
			    "target/speed",
			    localise("Speed") + ": " +
			        std::to_string(static_cast<std::int64_t>(std::hypot(double(contact.vx), double(contact.vy))) /
			                       1000) +
			        " m/s");
			result.emplace_back("target/faction",
			                    localise(contact.faction_label.empty() ? "Unaffiliated" : contact.faction_label) +
			                        " / " + localise(contact.disposition));
		}
	if (!found)
		result.emplace_back("target/empty", localise(selected ? "Selected contact is outside positional sensor coverage"
		                                                      : "Select a contact on the radar"));
	for (const auto &[key, text] : target_model.gameplay_readouts())
		if (selected && (key.starts_with("game.scan-finding/") || key.starts_with("game.scan-status/") ||
		                 key.starts_with("game.scan-progress/")))
			result.emplace_back("target/" + key, text);
	return result;
}
bool select_radar_contact(EntityId contact)
{
	const bool accepted =
	    inline_console_.empty() ? model_.select_contact(contact) : inline_model_.select_contact(contact);
	model_.act("game.sensor-contact/" + std::to_string(contact));
	prepare_dirty_ = render_dirty_ = render_urgent_ = true;
	return accepted;
}
void prepare_system_popup(const ConsoleRegion &region, const std::vector<EngineeringCard> &cards)
{
	popup_controls_.clear();
	ConsoleModel *detail_model = nullptr;
	std::string detail_prefix;
	if (!model_.status_system().empty())
		detail_model = &model_;
	else if (!inline_console_.empty() && !inline_model_.status_system().empty())
	{
		detail_model = &inline_model_;
		detail_prefix = "inline/";
	}
	else
		for (const auto &[console, source] : support_models_)
			if (!source->status_system().empty())
			{
				detail_model = source.get();
				detail_prefix = "support-console/" + console + "/";
				break;
			}
	const auto details = detail_model ? detail_model->system_details() : std::string{};
	std::string markup;
	std::vector<std::pair<std::string, std::string>> popup_readouts;
	auto esc = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	auto add = [&](const std::string &action, const std::string &label)
	{
		const auto name = id((action.find("game.control/") != std::string::npos || action.starts_with("game.team") ||
		                      action.starts_with("game.compartment/"))
		                         ? "popup-control/" + action
		                         : action);
		popup_controls_.emplace_back(name, label);
		return "<button id=\"" + esc(name) + "\">" + esc(label) + "</button>";
	};
	auto readout = [&](const std::string &key, const std::string &text)
	{
		const auto name = id("popup/" + key);
		popup_readouts.emplace_back(name, text);
		return "<p id=\"" + esc(name) + "\"></p>";
	};
	if (!details.empty())
	{
		markup = "<h1>" + esc(localise("System details")) + "</h1>" + readout("summary", details) +
		         add("systems", localise("All system statuses")) + add("systems-close", localise("Close"));
		auto &source = *detail_model;
		const auto system = source.status_system();
		if (source.observation())
			for (const auto &state : source.observation()->systems)
				if (state.id == system)
				{
					for (const auto &[key, value] : state.telemetry)
						markup +=
						    readout(system + "/" + key, localise(engineering_name(key)) + ": " + std::to_string(value));
					for (const auto &[key, text] : source.gameplay_readouts())
						if ((state.kind == OperatingSystem::Ftl && key.starts_with("game.readout/ftl")) ||
						    (state.kind == OperatingSystem::Psychic && key.starts_with("psychic.")) ||
						    (state.kind == OperatingSystem::Team &&
						     (key.starts_with("team.") || key.starts_with("medical."))) ||
						    (state.kind == OperatingSystem::Shield && key.starts_with("shield.")))
							markup += readout(key, text);
				}
		if (source.observation())
			for (const auto &[name, label] : source.gameplay_controls())
				if (name.starts_with("game.control/"))
					for (const auto &control : source.observation()->gameplay.value("controls", Json::array()))
						if (descriptor_action(control.at("id")) == name && !control.value("momentary", false))
						{
							const auto consumers = command_consumers(*source.observation(), control);
							if (std::find(consumers.begin(), consumers.end(), system) != consumers.end())
								markup += add(detail_prefix + name, label);
						}
		if (&source == &model_ && source.observation())
			for (const auto &state : source.observation()->systems)
				if (state.id == system && state.kind == OperatingSystem::Team)
					for (const auto &[action, label] : model_.asset_away_controls("team:" + system))
						markup += add(action, label);
	}
	else if (systems_open_ && support_popup_.starts_with("team/") && model_.observation())
	{
		const auto selected = support_popup_.substr(5);
		const auto &view = *model_.observation();
		markup = "<h1>" + esc(localise(engineering_name(selected))) + "</h1>";
		for (const auto &team : view.gameplay.value("teams", Json::array()))
			if (team.at("id") == selected && team.value("repair", false))
			{
				markup += readout("team/activity",
				                  localise(team.value("work", "Idle")) + " | " + localise(team.at("condition")));
				const auto parts = view.gameplay.value("combat", Json::object()).value("compartments", Json::array());
				for (const auto &part : parts)
					if (part.at("index").get<std::int64_t>() == team.at("location").get<std::int64_t>() + 1)
						markup += readout("team/location",
						                  localise("Section") + ": " + localise(engineering_name(part.at("id"))));
				const bool present = !team.value("reserved", false) && team.value("travel", 0) == 0 &&
				                     team.at("condition") != "lost" && team.at("condition") != "detained";
				if (present)
				{
					bool any = false;
					for (const auto &fault :
					     view.gameplay.value("combat", Json::object()).value("faults", Json::array()))
						if (fault.at("compartment_index").get<std::int64_t>() ==
						    team.at("location").get<std::int64_t>() + 1)
						{
							any = true;
							markup += readout(
							    "fault/" + fault.at("system").get<std::string>() + "/" +
							        fault.at("symptom").get<std::string>(),
							    localise(engineering_name(fault.at("system"))) + ": " + localise(fault.at("symptom")) +
							        (fault.value("diagnosed", false) ? " — " + localise(fault.at("diagnosis")) : ""));
						}
					if (!any)
						markup += readout("team/damage", localise("No observed system damage here"));
				}
				else
					markup += readout("team/damage", localise("System damage details require a team in the section"));
				for (const auto &[action, label] : model_.asset_away_controls("team:" + selected))
					markup += add(action, label);
				if (!team.value("reserved", false))
					for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
					{
						const auto consumers = command_consumers(view, descriptor);
						if (std::find(consumers.begin(), consumers.end(), selected) == consumers.end())
							continue;
						const auto action = descriptor.value("action", "");
						if (action == "repair_compartment")
						{
							const auto target = descriptor.at("value").get<std::int64_t>();
							if (target == 0)
								markup += add(descriptor_action(descriptor.at("id")), localise("Automatic deployment"));
							for (const auto &part : parts)
								if (part.at("index").get<std::int64_t>() == target)
									markup +=
									    add(descriptor_action(descriptor.at("id")),
									        localise("Send to") + " " + localise(engineering_name(part.at("id"))));
						}
						else if (action == "repair_system" && present)
						{
							const auto label = descriptor.value("label", action);
							bool observed = descriptor.at("value") == 0 || label == "Compartment hull";
							for (const auto &fault :
							     view.gameplay.value("combat", Json::object()).value("faults", Json::array()))
								observed |=
								    fault.at("system") == label && fault.at("compartment_index").get<std::int64_t>() ==
								                                       team.at("location").get<std::int64_t>() + 1;
							if (observed)
								markup +=
								    add(descriptor_action(descriptor.at("id")), localise(engineering_name(label)));
						}
					}
			}
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_ && support_popup_ == "medical" && model_.observation())
	{
		markup = "<h1>" + esc(localise("Medical and support teams")) + "</h1>";
		const auto teams = model_.observation()->gameplay.value("teams", Json::array());
		for (const auto &team : teams)
			if (!team.value("repair", false))
				markup += readout("team/" + team.at("id").get<std::string>(),
				                  localise(engineering_name(team.at("id"))) + " — " +
				                      localise(team.value("work", "Idle")) + " | " + localise(team.at("condition")));
		for (const auto &team : teams)
			if (!team.value("repair", false))
				for (const auto &[action, label] :
				     model_.asset_away_controls("team:" + team.at("id").get<std::string>()))
					markup += add(action, localise(engineering_name(team.at("id"))) + ": " + label);
		for (const auto &[key, text] : model_.gameplay_readouts())
			if (key.starts_with("medical."))
				markup += readout(key, text);
		for (const auto &[name, label] : model_.gameplay_controls())
		{
			bool allowed = name.starts_with("game.team-patient/");
			for (const auto &team : teams)
				if (!team.value("repair", false))
					allowed |= name.starts_with("game.team-order/" + team.at("id").get<std::string>() + "/") ||
					           name == "game.team-cancel/" + team.at("id").get<std::string>();
			if (allowed)
				markup += add(name, label);
		}
		for (const auto &part : model_.team_map())
			if (part.value("dispatch", false) && part.value("eligible", false))
				markup += add("game.compartment/" + std::to_string(part.at("index").get<std::int64_t>()),
				              localise("Send to") + " " + localise(engineering_name(part.at("id"))));
		markup += add("systems-close", localise("Close"));
	}
	else if (systems_open_)
	{
		markup = "<h1>" + esc(localise("System statuses")) + "</h1>";
		std::set<std::string> shown, dedicated;
		for (const auto &card : cards)
			dedicated.insert(card.id);
		std::vector<std::pair<ConsoleModel *, std::string>> sources{{&model_, ""}};
		if (!inline_console_.empty())
			sources.emplace_back(&inline_model_, "inline/");
		for (const auto &[console, source] : support_models_)
			sources.emplace_back(source.get(), "support-console/" + console + "/");
		for (const auto &[source, prefix] : sources)
			for (const auto &status : source->system_statuses())
				if (!dedicated.contains(status.system) && shown.insert(status.system).second)
					markup += "<p>" + add(prefix + "game.status/" + status.system, status.semantic_label) + "</p>";
		markup += add("systems-close", localise("Close"));
	}
	auto *dialog = document_->GetElementById("fault-dialog");
	if (markup != fault_dialog_signature_)
	{
		elements_.clear();
		properties_.clear();
		dialog->SetInnerRML(markup);
		fault_dialog_signature_ = markup;
	}
	property(dialog, "left", std::to_string(region.width * .1f) + "dp");
	property(dialog, "top", std::to_string(region.height * .15f) + "dp");
	property(dialog, "width", std::to_string(region.width * .8f) + "dp");
	property(dialog, "height", std::to_string(region.height * .65f) + "dp");
	property(dialog, "display", markup.empty() ? "none" : "block");
	context_->Update();
	if (!markup.empty())
	{
		const float width = std::max(44.f, region.width * .8f - 80.f);
		Rml::ElementList buttons;
		dialog->GetElementsByTagName(buttons, "button");
		for (auto *button : buttons)
		{
			property(button, "width", std::to_string(std::min(210.f, width)) + "dp");
			property(button, "min-height", "44dp");
		}
		for (int i = 0; i < dialog->GetNumChildren(); ++i)
			if (auto *child = dialog->GetChild(i); child->GetTagName() == "p" || child->GetTagName() == "h1")
				property(child, "width", std::to_string(width) + "dp");
	}
	if (!markup.empty())
	{
		std::erase_if(app_->screen_controls,
		              [&](const auto &entry)
		              {
			              return entry.first.starts_with(id(""));
		              });
		std::erase_if(app_->screen_inputs,
		              [&](const auto &entry)
		              {
			              return entry.id.starts_with(id(""));
		              });
	}
	for (const auto &[name, text] : popup_readouts)
	{
		if (auto *row = document_->GetElementById(name))
			leaf_markup(row, esc(text));
		app_->screen_readouts.emplace_back(name, text);
	}
	context_->Update();
	for (const auto &[name, label] : popup_controls_)
	{
		app_->screen_controls.emplace_back(name, label);
		place(*app_, region, name);
	}
}

std::string radar_choices_signature_;
void prepare_radar_choices(const ConsoleRegion &region)
{
	if (!model_.layout() || !model_.observation())
		return;
	const auto &view = *model_.observation();
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("kind") == "radar")
		{
			auto *dial = document_->GetElementById("dial/" + widget.at("id").get<std::string>());
			if (!dial || !dial->IsVisible(true))
				continue;
			const auto chosen =
			    select_system(view.systems, OperatingSystem::Radar, model_.widget_system(widget.at("id")), false);
			for (const auto &sensor : view.systems)
				if (chosen && sensor.id == *chosen && sensor.maximum > 0)
				{
					if (!sensor.enabled)
					{
						if (dial->GetNumChildren())
						{
							elements_.clear();
							properties_.clear();
							dial->SetInnerRML("");
							radar_choices_signature_.clear();
						}
						return;
					}
					std::vector<std::pair<EntityId, Vector2>> points;
					std::map<EntityId, std::string> labels;
					const Vector2 centre{dial->GetOffsetWidth() / 2, dial->GetOffsetHeight() / 2};
					const float radius = std::min(centre.x, centre.y) * .66f;
					for (const auto &body : view.contacts)
						if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) !=
						        sensor.contact_ids.end() &&
						    std::hypot(double(body.x - view.own.x), double(body.y - view.own.y)) <= sensor.maximum)
						{
							points.emplace_back(
							    body.id,
							    Vector2{centre.x + float(double(body.x - view.own.x) / sensor.maximum) * radius,
							            centre.y - float(double(body.y - view.own.y) / sensor.maximum) * radius});
							const auto &catalog = app_->session->catalog();
							labels[body.id] =
							    localise("Select target") + ": " +
							    localise(catalog.entities.contains(body.definition)
							                 ? catalog.entities.at(body.definition).value("name", body.definition)
							                 : body.definition) +
							    " #" + std::to_string(body.id);
						}
					for (const auto &body : view.bearing_contacts)
						if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) !=
						    sensor.contact_ids.end())
						{
							points.emplace_back(body.id, bearing_ring_point(centre, radius, body.bearing_mdeg));
							labels[body.id] = localise("Select bearing contact") + " #" + std::to_string(body.id);
						}
					std::string markup;
					for (const auto &[target, point] : points)
						markup += "<button class=\"radar-contact\" id=\"" +
						          id("radar-contact/" + std::to_string(target)) + "\" aria-label=\"" +
						          Rml::StringUtilities::EncodeRml(labels[target]) + "\"/>";
					if (radar_choices_signature_ != markup || dial->GetNumChildren() != int(points.size()))
					{
						elements_.clear();
						properties_.clear();
						dial->SetInnerRML(markup);
						radar_choices_signature_ = markup;
					}
					for (const auto &[target, point] : points)
					{
						const auto name = id("radar-contact/" + std::to_string(target));
						auto *button = document_->GetElementById(name);
						property(button, "left", std::to_string(point.x - 12) + "px");
						property(button, "top", std::to_string(point.y - 12) + "px");
						app_->screen_controls.emplace_back(name, labels[target]);
					}
					context_->Update();
					for (const auto &[target, point] : points)
						place(*app_, region, id("radar-contact/" + std::to_string(target)));
					return;
				}
		}
}
