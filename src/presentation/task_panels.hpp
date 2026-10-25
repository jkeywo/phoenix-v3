// Registered task components. Each consumes only its permitted model/command family.
std::map<std::string, std::string> task_actions_;
std::vector<std::pair<ConsoleModel *, std::string>> task_sources()
{
	std::vector<std::pair<ConsoleModel *, std::string>> sources{{&model_, ""}};
	for (auto &[console, source] : support_models_)
		sources.emplace_back(source.get(), "support-console/" + console + "/");
	if (!inline_console_.empty())
		sources.emplace_back(&inline_model_, "inline/");
	return sources;
}
void prepare_task_panels(const ConsoleRegion &region)
{
	task_actions_.clear();
	for (const char *name : {"weapons", "objectives", "viewscreen-controls", "alert-controls", "shield-display",
	                         "ftl-controls", "compartment-health", "team-roster"})
	{
		if (!authored_component_visible(name))
			continue;
		auto *panel = authored_component(name);
		std::string markup;
		std::vector<std::pair<std::string, std::string>> buttons, readouts;
		std::map<std::string, bool> toggles;
		bool weapon_overflow = false;
		auto esc = [](const std::string &value)
		{
			return Rml::StringUtilities::EncodeRml(value);
		};
		auto line = [&](const std::string &key, const std::string &value)
		{
			const auto field = id(std::string(name) + "/" + key);
			readouts.emplace_back(field, value);
			return "<p id=\"" + field + "\">" + esc(value) + "</p>";
		};
		auto button = [&](const std::string &action, const std::string &label, const std::string &state = std::string{})
		{
			const auto key = "task/" + std::string(name) + "/" + std::to_string(buttons.size());
			task_actions_[key] = action;
			buttons.emplace_back(id(key), label);
			if (!state.empty())
				toggles[id(key)] = state == "on";
			return "<button id=\"" + esc(id(key)) + "\" class=\"task-button " + state + "\"" +
			       (state.empty() ? "" : " aria-pressed=\"" + std::string(state == "on" ? "true" : "false") + "\"") +
			       ">" + (state.empty() ? "" : "<span class=\"task-state-icon\"></span>") + esc(label) + "</button>";
		};
		const std::string component = name;
		const auto logical_height = panel->GetClientHeight() / context_->GetDensityIndependentPixelRatio();
		const auto rows = static_cast<std::size_t>(
		    std::max(1.f, std::floor((logical_height - 110.f) / std::max(50.f, 70.f * text_scale_ / 100.f))));

		if (component == "weapons" || component == "shield-display")
		{
			markup =
			    "<h2>" + localise(component == "weapons" ? "Weapons" : "Shields") + "</h2><div class=\"task-toggles\">";
			std::set<std::string> shown;
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &system : source->observation()->systems)
						if (system.kind ==
						        (component == "weapons" ? OperatingSystem::Weapon : OperatingSystem::Shield) &&
						    !shown.contains(system.id))
						{
							std::string action;
							for (const auto &descriptor :
							     source->observation()->gameplay.value("controls", Json::array()))
								if (descriptor.value("action", "") == "system_enabled" &&
								    descriptor.value("value", -1) == (system.enabled ? 0 : 1))
								{
									const auto consumers = command_consumers(*source->observation(), descriptor);
									if (std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
									{
										action = prefix + descriptor_action(descriptor.at("id"));
										break;
									}
								}
							if (component == "weapons" && action.empty())
								continue;
							shown.insert(system.id);
							if (component == "weapons" && shown.size() > rows * 2)
								continue;
							const auto label = localise(engineering_name(system.id));
							if (component == "weapons")
							{
								if (!action.empty())
									markup += button(action, label, system.enabled ? "on" : "off");
							}
							else
							{
								std::int64_t strength = 0, capacity = 0;
								for (const auto &shield : source->observation()
								                              ->gameplay.value("combat", Json::object())
								                              .value("shields", Json::array()))
									if (shield.value("id", "") == system.id)
									{
										strength += shield.value("strength", 0LL);
										capacity += shield.value("capacity", 0LL);
									}
								markup += "<div class=\"shield-sector " + std::string(system.enabled ? "on" : "off") +
								          "\">" +
								          (action.empty() ? "<h3>" + esc(label) + "</h3>"
								                          : button(action, label, system.enabled ? "on" : "off")) +
								          "<progress value=\"" + std::to_string(strength) + "\" max=\"" +
								          std::to_string(std::max<std::int64_t>(1, capacity)) + "\"/>" +
								          line(system.id, std::to_string(strength) + " / " + std::to_string(capacity)) +
								          "</div>";
							}
						}
			markup += "</div>";
			if (component == "weapons" && shown.size() > rows * 2)
			{
				weapon_overflow = true;
				markup += button("task-detail/weapons", localise("All weapons"));
			}
		}
		else if (component == "objectives")
		{
			markup = "<h2>" + localise("Objectives") + "</h2>";
			std::size_t count = 0;
			for (const auto &objective : model_.objectives())
				if (count++ < 3)
					markup +=
					    line(std::to_string(objective.value("id", 0ULL)),
					         localise(objective.value("title", "")) + " — " + localise(objective.value("status", "")));
			if (count > 3)
				markup += button("task-detail/objectives", localise("All objectives"));
		}
		else if (component == "team-roster" || component == "compartment-health")
		{
			markup = "<h2>" + localise(component == "team-roster" ? "Teams" : "Compartments") + "</h2>";
			if (model_.observation())
			{
				const auto &view = *model_.observation();
				if (component == "compartment-health")
				{
					std::size_t count = 0;
					auto parts = view.gameplay.value("combat", Json::object()).value("compartments", Json::array());
					std::stable_sort(parts.begin(), parts.end(),
					                 [](const auto &a, const auto &b)
					                 {
						                 return a.value("damaged", false) && !b.value("damaged", false);
					                 });
					const auto limit = parts.size() > 6 ? 4u : 6u;
					for (const auto &part : parts)
						if (count++ < limit)
							markup += "<div class=\"health-row\"><span>" +
							          esc(localise(engineering_name(part.at("id")))) + "</span><progress value=\"" +
							          part.at("hp").dump() + "\" max=\"" + part.at("maximum").dump() + "\"/></div>";
					if (count > limit)
						markup += button("task-detail/compartments", localise("All compartments"));
				}
				else
				{
					std::size_t count = 0;
					for (const auto &team : view.gameplay.value("teams", Json::array()))
						if (count++ < rows)
							markup += button(team.value("repair", false)
							                     ? "support-details/team/" + team.at("id").get<std::string>()
							                     : "support-details/medical",
							                 localise(engineering_name(team.at("id"))) + " — " +
							                     localise(team.value("work", "Ready")));
					if (count > rows)
						markup += button("task-detail/teams", localise("All teams"));
				}
			}
		}
		else
		{
			markup = "<h2>" +
			         localise(component == "ftl-controls"          ? "FTL"
			                  : component == "viewscreen-controls" ? "Viewscreen"
			                                                       : "Alert condition") +
			         "</h2><div class=\"task-choices\">";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
				{
					for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
					{
						const auto action = descriptor.value("action", "");
						bool include = component == "viewscreen-controls" &&
						               (action == "camera_mode" || action == "camera_direction");
						include |= component == "alert-controls" && action == "alert.set";
						if (component == "ftl-controls" && action == "ftl.depart")
							include = true;
						if (component == "ftl-controls")
							for (const auto &consumer : command_consumers(*source->observation(), descriptor))
								for (const auto &system : source->observation()->systems)
									if (consumer == system.id && system.kind == OperatingSystem::Ftl)
										include = true;
						if (include)
							markup += button(
							    prefix + descriptor_action(descriptor.at("id")),
							    localise(component == "ftl-controls"
							                 ? (action == "ftl.depart"         ? "Depart"
							                    : descriptor.value("value", 0) ? "Prepare FTL"
							                                                   : "Cancel FTL")
							                 : descriptor.value("label_key", descriptor.value("label", action))));
					}
					if (component == "ftl-controls")
						for (const auto &ftl : source->observation()->gameplay.value("ftl", Json::array()))
							markup +=
							    line("status",
							         localise(ftl.value("ready", false) ? "Ready" : ftl.value("blocker", "Preparing")));
				}
			markup += "</div>";
		}
		if (component == "objectives" || (component == "weapons" && !weapon_overflow))
			markup += button("systems", localise("System status"));
		if (station_component_markup_[component] != markup)
		{
			const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			panel->SetInnerRML(markup);
			station_component_markup_[component] = markup;
			elements_.clear();
			if (auto *previous = document_->GetElementById(focus))
				previous->Focus();
		}
		context_->Update();
		for (const auto &[key, label] : buttons)
		{
			app_->screen_controls.emplace_back(key, label);
			place(*app_, region, key);
		}
		for (const auto &[key, value] : readouts)
		{
			app_->screen_readouts.emplace_back(key, value);
			place(*app_, region, key);
		}
		for (const auto &[key, state] : toggles)
			app_->screen_toggles[key] = state;
	}
}
