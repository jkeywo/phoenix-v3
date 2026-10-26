// Registered task components. Each consumes only its permitted model/command family.
std::map<std::string, std::string> task_actions_;
std::map<int, std::string> condition_choices_;
std::optional<std::pair<int, std::uint64_t>> condition_pending_;
int condition_selected_ = 0;
std::vector<Json> shield_sectors_;
std::optional<SpriteMaterial> shield_ship_sprite_;
Rectangle shield_ship_crop_{};
std::string shield_ship_definition_;
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
	condition_choices_.clear();
	shield_sectors_.clear();
	for (const char *name : {"weapons", "objectives", "viewscreen-controls", "alert-controls", "shield-display",
	                         "ftl-controls", "compartment-health", "team-roster"})
	{
		if (!authored_component_visible(name))
			continue;
		auto *panel = authored_component(name);
		std::string markup;
		std::vector<std::pair<std::string, std::string>> buttons, readouts;
		std::map<std::string, bool> toggles;
		std::vector<std::tuple<std::string, std::string, bool>> weapon_choices;
		auto esc = [](const std::string &value)
		{
			return Rml::StringUtilities::EncodeRml(value);
		};
		auto line = [&](const std::string &key, const std::string &value)
		{
			const auto field = id(std::string(name) + "/" + key);
			readouts.emplace_back(field, value);
			return "<p id=\"" + field + "\">" + (std::string(name) == "shield-display" ? "" : esc(value)) + "</p>";
		};
		auto button = [&](const std::string &action, const std::string &label, const std::string &state = std::string{},
		                  bool enabled = true)
		{
			const auto key = "task/" + std::string(name) + "/" + std::to_string(buttons.size());
			if (enabled)
				task_actions_[key] = action;
			buttons.emplace_back(id(key), label);
			if (!state.empty())
				toggles[id(key)] = state == "on";
			return "<button id=\"" + esc(id(key)) + "\" class=\"task-button " + state + (enabled ? "" : " disabled") +
			       "\"" +
			       (state.empty() ? "" : " aria-pressed=\"" + std::string(state == "on" ? "true" : "false") + "\"") +
			       (enabled ? ">" : " disabled aria-disabled=\"true\" style=\"tab-index:none\">") +
			       (state.empty() ? "" : "<span class=\"task-state-icon\"></span>") + esc(label) + "</button>";
		};
		const std::string component = name;
		const auto logical_height = panel->GetClientHeight() / context_->GetDensityIndependentPixelRatio();
		const auto rows = static_cast<std::size_t>(
		    std::max(1.f, std::floor((logical_height - 110.f) / std::max(50.f, 70.f * text_scale_ / 100.f))));

		if (component == "weapons")
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
							const auto label = localise(engineering_name(system.id));
							weapon_choices.emplace_back(action, label, system.enabled);
						}

			const auto weapon_row_height = std::max(50.f, 70.f * text_scale_ / 100.f);
			auto weapon_rows =
			    static_cast<std::size_t>(std::max(1.f, std::floor((logical_height - 50.f) / weapon_row_height)));
			if (weapon_choices.size() > weapon_rows * 2)
				weapon_rows = rows; // Reserve the contextual-list button only when it is needed.
			for (std::size_t i = 0; i < std::min(weapon_choices.size(), weapon_rows * 2); ++i)
			{
				const auto &[action, label, enabled] = weapon_choices[i];
				markup += button(action, label, enabled ? "on" : "off");
			}
			markup += "</div>";
			if (weapon_choices.size() > weapon_rows * 2)
				markup += button("task-detail/weapons", localise("All weapons"));
		}
		else if (component == "shield-display")
		{
			markup = "<h2>" + localise("Shields") + "</h2><div class=\"shield-diagram\" id=\"" + id("shield-diagram") +
			         "\" style=\"height:" + std::string(text_scale_ > 100 ? "55%" : "65%") + "\"></div>";
			std::set<std::string> seen, controlled;
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
				{
					const auto &view = *source->observation();
					for (auto shield : view.gameplay.value("combat", Json::object()).value("shields", Json::array()))
						if (seen.insert(shield.at("component_id").get<std::string>()).second)
						{
							shield["enabled"] = true;
							for (const auto &system : view.systems)
								if (system.id == shield.value("id", ""))
									shield["enabled"] = system.enabled;
							shield_sectors_.push_back(shield);
							const auto capacity = shield.value("capacity", 0LL),
							           strength = shield.value("strength", 0LL);
							markup += line("sector/" + shield.at("component_id").get<std::string>(),
							               localise(engineering_name(shield.value("id", ""))) + ": " +
							                   std::to_string(capacity ? strength * 100 / capacity : 0) + "%" +
							                   (shield.at("enabled").get<bool>() ? "" : " — " + localise("Off")));
						}
					for (const auto &system : view.systems)
						if (system.kind == OperatingSystem::Shield && !controlled.contains(system.id))
							for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
								if (descriptor.value("action", "") == "system_enabled" &&
								    descriptor.value("value", -1) == (system.enabled ? 0 : 1))
								{
									const auto consumers = command_consumers(view, descriptor);
									if (std::find(consumers.begin(), consumers.end(), system.id) == consumers.end())
										continue;
									controlled.insert(system.id);
									markup +=
									    button(prefix + descriptor_action(descriptor.at("id")),
									           localise(engineering_name(system.id)), system.enabled ? "on" : "off");
									break;
								}
				}
		}
		else if (component == "alert-controls")
		{
			markup = "<h2>" + localise("Condition") + "</h2>";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						if (descriptor.value("action", "") == "alert.set")
						{
							const auto key = descriptor.at("id").get<std::string>();
							const int number = key.size() > 11 && key.substr(key.size() - 11, 10) == "condition-" &&
							                           key.back() >= '1' && key.back() <= '5'
							                       ? key.back() - '0'
							                       : descriptor.value("value", 0) + 1;
							condition_choices_[number] = prefix + descriptor_action(key);
							if (descriptor.value("selected", false))
							{
								condition_selected_ = number;
							}
						}
			if (!condition_choices_.empty())
			{
				markup += "<input class=\"condition-slider\" id=\"" + id("support-condition") +
				          "\" type=\"range\" min=\"" + std::to_string(condition_choices_.begin()->first) + "\" max=\"" +
				          std::to_string(condition_choices_.rbegin()->first) +
				          "\" step=\"1\"/><div class=\"condition-ticks\">";
				for (const auto &[number, choice] : condition_choices_)
					markup += "<span>" + std::to_string(number) + "</span>";
				markup += "</div>";
			}
		}
		else if (component == "objectives")
		{
			markup = "<h2>" + localise("Objectives") + "</h2>";
			for (const auto &objective : model_.objectives())
			{
				const auto status = objective.value("status", Json{});
				static constexpr const char *statuses[]{"Pending", "Active", "Completed", "Failed", "Cancelled"};
				const auto label = status.is_number_integer()
				                       ? std::string(statuses[std::clamp(status.get<int>(), 0, 4)])
				                   : status.is_string() ? status.get<std::string>()
				                                        : std::string{};
				markup += line(std::to_string(objective.value("id", 0ULL)),
				               localise(objective.value("title", "")) + " — " + localise(label));
			}
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
		else if (component == "ftl-controls")
		{
			markup = "<h2>" + localise("FTL") + "</h2><div class=\"task-choices\">";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &ftl : source->observation()->gameplay.value("ftl", Json::array()))
					{
						const auto system = ftl.value("system", "");
						const bool preparing = ftl.value("enabled", false), ready = ftl.value("ready", false);
						std::string toggle, depart;
						for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						{
							if (descriptor.value("action", "") == "ftl.depart")
								depart = prefix + descriptor_action(descriptor.at("id"));
							if (descriptor.value("action", "") != "system_enabled" ||
							    descriptor.value("value", -1) != (preparing ? 0 : 1))
								continue;
							const auto consumers = command_consumers(*source->observation(), descriptor);
							if (std::find(consumers.begin(), consumers.end(), system) != consumers.end())
								toggle = prefix + descriptor_action(descriptor.at("id"));
						}
						if (!toggle.empty())
							markup += button(toggle, localise(preparing ? "Cancel FTL" : "Prepare FTL"));
						if (!depart.empty())
							markup += button(depart, localise("Depart"), "", ready);
						markup += line("status", localise(ready ? "Ready" : ftl.value("blocker", "Preparing")));
					}
			markup += "</div>";
		}
		else
		{
			markup = "<h2>" + localise("Viewscreen") + "</h2><div class=\"task-choices\">";
			for (auto &[source, prefix] : task_sources())
				if (source->observation())
					for (const auto &descriptor : source->observation()->gameplay.value("controls", Json::array()))
						if (descriptor.value("action", "") == "camera_mode" ||
						    descriptor.value("action", "") == "camera_direction")
							markup +=
							    button(prefix + descriptor_action(descriptor.at("id")),
							           localise(descriptor.value("label_key", descriptor.value("label", "Camera"))));
			markup += "</div>";
		}
		if (component == "objectives")
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
		if (component == "alert-controls" && !condition_choices_.empty())
		{
			auto selected = condition_selected_;
			if (condition_pending_)
			{
				if (condition_pending_->first != selected && app_->session->ticks() < condition_pending_->second)
					selected = condition_pending_->first;
				else
					condition_pending_.reset();
			}
			syncing_values_ = true;
			if (auto *field =
			        dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("support-condition"))))
				if (field->GetValue() != std::to_string(selected))
					field->SetValue(std::to_string(selected));
			syncing_values_ = false;
			app_->screen_inputs.push_back(
			    {id("support-condition"), localise("Condition"), std::to_string(selected), true});
			place(*app_, region, id("support-condition"));
		}
		context_->Update();
		for (const auto &[key, label] : buttons)
		{
			app_->screen_controls.emplace_back(key, label);
			place(*app_, region, key);
		}
		for (const auto &[key, value] : readouts)
		{
			if (component == "shield-display")
				if (auto *field = document_->GetElementById(key))
				{
					if (field->GetInnerRML() != esc(value))
						field->SetInnerRML(esc(value));
					if (auto *canvas = document_->GetElementById(id("shield-diagram")))
					{
						const auto offset = canvas->GetAbsoluteOffset(Rml::BoxArea::Border) -
						                    panel->GetAbsoluteOffset(Rml::BoxArea::Border);
						const float radius = std::min(canvas->GetClientWidth(), canvas->GetClientHeight()) * .43f;
						const float centre_x = offset.x + canvas->GetClientWidth() / 2,
						            centre_y = offset.y + canvas->GetClientHeight() / 2;
						bool right = true;
						for (const auto &sector : shield_sectors_)
							if (key == id("shield-display/sector/" + sector.at("component_id").get<std::string>()))
								right = std::cos(float(sector.value("bearing_mdeg", 0LL) -
								                       model_.observation()->own.heading) /
								                 1000 * DEG2RAD) >= 0;
						const float width = std::min(panel->GetClientWidth() * .25f,
						                             210.f * context_->GetDensityIndependentPixelRatio());
						property(field, "position", "absolute");
						property(field, "width", std::to_string(width) + "px");
						property(field, "left",
						         std::to_string(right ? centre_x + radius + 12 : centre_x - radius - width - 12) +
						             "px");
						property(field, "top", std::to_string(centre_y - field->GetOffsetHeight() / 2) + "px");
						property(field, "margin", "0px");
						property(field, "text-align", right ? "left" : "right");
					}
				}
			app_->screen_readouts.emplace_back(key, value);
			place(*app_, region, key);
		}
		for (const auto &[key, state] : toggles)
			app_->screen_toggles[key] = state;
	}
}

void draw_support_shields(float ox, float oy)
{
	if (settings_open_ || systems_open_ || !authored_component_visible("shield-display") || !model_.observation())
		return;
	auto *panel = document_->GetElementById(id("shield-diagram"));
	if (!panel || !panel->IsVisible(true))
		return;
	const auto at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Vector2 centre{ox + at.x + panel->GetClientWidth() / 2, oy + at.y + panel->GetClientHeight() / 2};
	const float radius = std::min(panel->GetClientWidth(), panel->GetClientHeight()) * .43f;
	if (radius < 12)
		return;
	const auto &view = *model_.observation();
	const auto &catalog = app_->session->catalog();
	if (shield_ship_definition_ != view.own.definition)
	{
		shield_ship_definition_ = view.own.definition;
		shield_ship_sprite_.reset();
		const auto &render = catalog.entities.at(view.own.definition).at("render");
		if (render.contains("sprite"))
		{
			shield_ship_sprite_ = compile_sprite(render.at("sprite"), catalog, "shield ship image");
			const auto &sprite = *shield_ship_sprite_;
			const auto &frame = sprite_frame(sprite, 0, 270);
			auto image = load_asset_image(catalog.assets.at(sprite.albedo));
			auto *pixels = LoadImageColors(image);
			int left = frame.rect[0] + frame.rect[2], top = frame.rect[1] + frame.rect[3], right = -1, bottom = -1;
			if (pixels)
				for (int y = frame.rect[1]; y < frame.rect[1] + frame.rect[3]; ++y)
					for (int x = frame.rect[0]; x < frame.rect[0] + frame.rect[2]; ++x)
						if (pixels[y * image.width + x].a > 16)
						{
							left = std::min(left, x);
							top = std::min(top, y);
							right = std::max(right, x);
							bottom = std::max(bottom, y);
						}
			shield_ship_crop_ =
			    right >= left && bottom >= top
			        ? Rectangle{float(left), float(top), float(right - left + 1), float(bottom - top + 1)}
			        : Rectangle{float(frame.rect[0]), float(frame.rect[1]), float(frame.rect[2]), float(frame.rect[3])};
			if (pixels)
				UnloadImageColors(pixels);
			UnloadImage(image);
		}
	}
	if (shield_ship_sprite_)
	{
		const auto &sprite = *shield_ship_sprite_;
		const auto &frame = sprite_frame(sprite, 0, 270);
		const auto texture = radar_textures_.load(sprite.albedo, catalog.assets.at(sprite.albedo));
		const auto crop = shield_ship_crop_;
		const float scale = radius * 1.35f / std::max(crop.width, crop.height);
		const float w = crop.width * scale, h = crop.height * scale;
		DrawTexturePro(texture, {crop.x, crop.y, crop.width * (frame.mirror_x ? -1.f : 1.f), crop.height},
		               {centre.x, centre.y, w, h}, {w / 2, h / 2}, 0, WHITE);
	}
	else
		draw_radar_icon(RadarIcon::ship, centre, radius * .55f, 0, WHITE);
	for (const auto &shield : shield_sectors_)
	{
		const float bearing = (shield.value("bearing_mdeg", 0LL) - view.own.heading) / 1000.f;
		// Two pools use fore/aft status semicircles rather than a tactical coverage plot.
		const float arc = shield_sectors_.size() == 2 ? 180.f : shield.value("arc_mdeg", 360000LL) / 1000.f;
		const auto capacity = shield.value("capacity", 0LL);
		const float fraction =
		    capacity ? std::clamp(float(double(shield.value("strength", 0LL)) / capacity), 0.f, 1.f) : 0;
		const Color colour = !shield.value("enabled", true) ? GRAY
		                     : fraction < .25f              ? RED
		                     : fraction < .6f               ? ORANGE
		                                                    : SKYBLUE;
		const int halves = arc >= 359 ? 2 : 1;
		for (int half = 0; half < halves; ++half)
		{
			const float width = arc / halves, begin = bearing - arc / 2 + width * half + 3, end = begin + width - 6;
			auto stroke = [&](float to, Color tint, float thickness)
			{
				for (int i = 0; i < 96; ++i)
				{
					const float a = (begin + (to - begin) * i / 96) * DEG2RAD,
					            b = (begin + (to - begin) * (i + 1) / 96) * DEG2RAD;
					DrawLineEx({centre.x + std::cos(a) * radius, centre.y - std::sin(a) * radius},
					           {centre.x + std::cos(b) * radius, centre.y - std::sin(b) * radius}, thickness, tint);
				}
			};
			stroke(end, {45, 66, 73, 255}, std::max(6.f, radius * .065f));
			if (fraction > 0)
				stroke(begin + (end - begin) * fraction, colour, std::max(4.f, radius * .045f));
		}
	}
}
