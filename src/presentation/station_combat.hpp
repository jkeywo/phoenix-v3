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
	const auto selected = model_.status_system();
	const auto inline_selected = inline_model_.status_system();
	const auto details = !selected.empty()                                      ? model_.system_details()
	                     : !inline_console_.empty() && !inline_selected.empty() ? inline_model_.system_details()
	                                                                            : std::string{};
	std::string markup;
	auto esc = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	auto add = [&](const std::string &action, const std::string &label)
	{
		popup_controls_.emplace_back(id(action), label);
		return "<button id=\"" + esc(id(action)) + "\">" + esc(label) + "</button>";
	};
	if (!details.empty())
	{
		markup = "<h1>" + esc(localise("System details")) + "</h1><p>" + esc(details) + "</p>" +
		         add("systems", localise("All system statuses")) + add("systems-close", localise("Close"));
		auto &source = !selected.empty() ? model_ : inline_model_;
		const auto system = !selected.empty() ? selected : inline_selected;
		if (source.observation())
			for (const auto &[name, label] : source.gameplay_controls())
				if (name.starts_with("game.control/"))
					for (const auto &control : source.observation()->gameplay.value("controls", Json::array()))
						if (descriptor_action(control.at("id")) == name)
						{
							const auto consumers = command_consumers(*source.observation(), control);
							if (std::find(consumers.begin(), consumers.end(), system) != consumers.end())
								markup += add((&source == &inline_model_ ? "inline/" : "") + name, label);
						}
	}
	else if (systems_open_)
	{
		markup = "<h1>" + esc(localise("System statuses")) + "</h1>";
		std::set<std::string> shown, dedicated;
		for (const auto &card : cards)
			dedicated.insert(card.id);
		for (auto *source : {&model_, &inline_model_})
		{
			if (source == &inline_model_ && inline_console_.empty())
				continue;
			for (const auto &status : source->system_statuses())
				if (!dedicated.contains(status.system) && shown.insert(status.system).second)
					markup +=
					    "<p>" +
					    add((source == &inline_model_ ? "inline/" : "") + std::string("game.status/") + status.system,
					        status.semantic_label) +
					    "</p>";
		}
		markup += add("systems-close", localise("Close"));
	}
	auto *dialog = document_->GetElementById("fault-dialog");
	if (markup != fault_dialog_signature_)
	{
		elements_.clear();
		dialog->SetInnerRML(markup);
		fault_dialog_signature_ = markup;
	}
	property(dialog, "display", markup.empty() ? "none" : "block");
	context_->Update();
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
