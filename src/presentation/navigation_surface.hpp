// ConsoleSurface members: presentation only, commands still pass through ConsoleModel.
NavigationCamera navigation_camera_;
Rectangle navigation_area_{};
bool navigation_visible_ = false;
std::string navigation_markup_, mission_outcome_;
std::map<std::uint64_t, Vector2> navigation_contacts_, navigation_starts_;
bool navigation_dragged_ = false;
std::vector<Rectangle> navigation_buttons_;
double navigation_x_ = 0, navigation_y_ = 0;

bool navigation_action(const std::string &action)
{
	if (action == "mission.leave")
	{
		if (!mission_outcome_.empty() && app_->return_home)
			app_->return_home();
		return true;
	}
	if (!navigation_visible_ || !action.starts_with("nav."))
		return false;
	if (action == "nav.reset")
		navigation_camera_.automatic = true;
	else if (action == "nav.in")
		navigation_camera_.zoom(.8);
	else if (action == "nav.out")
		navigation_camera_.zoom(1.25);
	else if (action == "nav.left")
		navigation_camera_.pan(80, 0);
	else if (action == "nav.right")
		navigation_camera_.pan(-80, 0);
	else if (action == "nav.up")
		navigation_camera_.pan(0, 80);
	else if (action == "nav.down")
		navigation_camera_.pan(0, -80);
	else if (action == "nav.clear")
		model_.set_waypoint(0, 0, false);
	else if (action == "nav.set")
		model_.set_waypoint(static_cast<std::int64_t>(navigation_x_), static_cast<std::int64_t>(navigation_y_));
	else if (action.starts_with("nav.objective/"))
		for (const auto &objective : model_.objectives())
			if (objective.contains("destination_x") &&
			    std::to_string(objective.at("id").get<std::uint64_t>()) == action.substr(14))
				model_.set_waypoint(objective.at("destination_x"), objective.at("destination_y"));
	return true;
}
bool navigation_pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
{
	if (!navigation_visible_)
		return false;
	if (kind == DeviceEventKind::Down)
		for (const auto &button : navigation_buttons_)
			if (CheckCollisionPointRec({x, y}, button))
				return false;
	if (kind == DeviceEventKind::Down && CheckCollisionPointRec({x, y}, navigation_area_))
	{
		if (navigation_contacts_.empty())
			navigation_dragged_ = false;
		navigation_contacts_[contact] = navigation_starts_[contact] = {x, y};
		return true;
	}
	if (!navigation_contacts_.contains(contact))
		return false;
	const auto old = navigation_contacts_.at(contact);
	if (kind == DeviceEventKind::Cancel)
		navigation_dragged_ = true;
	else if (navigation_contacts_.size() == 2)
	{
		for (const auto &[other, point] : navigation_contacts_)
			if (other != contact)
			{
				const auto before = std::hypot(old.x - point.x, old.y - point.y),
				           after = std::hypot(x - point.x, y - point.y);
				if (before > 5 && after > 5)
					navigation_camera_.zoom(before / after,
					                        (x + point.x) / 2 - navigation_area_.x - navigation_area_.width / 2,
					                        (y + point.y) / 2 - navigation_area_.y - navigation_area_.height / 2);
			}
		navigation_dragged_ = true;
	}
	else
	{
		const auto start = navigation_starts_.at(contact);
		if (std::hypot(x - start.x, y - start.y) > 5)
			navigation_dragged_ = true;
		if (navigation_dragged_)
			navigation_camera_.pan(x - old.x, y - old.y);
		else if (kind == DeviceEventKind::Up)
		{
			const double wx =
			    navigation_camera_.x + (x - navigation_area_.x - navigation_area_.width / 2) * navigation_camera_.scale;
			const double wy = navigation_camera_.y -
			                  (y - navigation_area_.y - navigation_area_.height / 2) * navigation_camera_.scale;
			const auto waypoint = model_.waypoint();
			const bool clear = waypoint.value("active", false) &&
			                   std::hypot(wx - waypoint.value("x", 0.0), wy - waypoint.value("y", 0.0)) <
			                       navigation_camera_.scale * 12;
			model_.set_waypoint(static_cast<std::int64_t>(std::clamp(wx, -1e9, 1e9)),
			                    static_cast<std::int64_t>(std::clamp(wy, -1e9, 1e9)), !clear);
		}
	}
	if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
	{
		navigation_contacts_.erase(contact);
		navigation_starts_.erase(contact);
	}
	else
		navigation_contacts_[contact] = {x, y};
	return true;
}
void prepare_navigation(Application &app, const ConsoleRegion &region, bool pregame)
{
	navigation_visible_ = false;
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			navigation_visible_ |= widget.at("kind") == "navigation";
	const auto objectives = model_.objectives();
	const auto report =
	    model_.observation() ? model_.observation()->gameplay.value("mission_report", Json(nullptr)) : Json(nullptr);
	const auto ftl =
	    model_.observation() && !pregame ? model_.observation()->gameplay.value("ftl", Json::array()) : Json::array();
	const bool navigator_panel = !ftl.empty() && model_.layout() && model_.layout()->at("widgets").empty();
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("kind") == "objectives")
				if (auto *target = document_->GetElementById("value/" + widget.at("id").get<std::string>()))
				{
					std::string text;
					for (const auto &objective : objectives)
						text += "<p>" +
						        Rml::StringUtilities::EncodeRml(localise(objective.value("title", "")) + ": " +
						                                        localise(objective.value("outcome", "").empty()
						                                                     ? objective.value("status", "")
						                                                     : objective.value("outcome", "")) +
						                                        ". " + localise(objective.value("description", ""))) +
						        "</p>";
					target->SetInnerRML(text);
				}
	mission_outcome_.clear();
	for (const auto &objective : objectives)
		if (!objective.value("outcome", "").empty())
			mission_outcome_ = objective.at("outcome");
	if (!report.is_null())
		mission_outcome_ = "ftl";
	if (pregame)
		mission_outcome_.clear();
	if (!mission_outcome_.empty())
		navigation_visible_ = false;
	document_->SetClass("navigation-console", navigation_visible_);
	if (navigation_visible_ || navigator_panel || !mission_outcome_.empty())
		document_->GetElementById("gameplay")->SetProperty("display", "none");
	navigation_area_ = {0, 110, region.width, std::max(50.f, region.height - 250)};
	if (navigation_visible_ && model_.observation())
	{
		const auto &own = model_.observation()->own;
		double left = own.x, right = own.x, bottom = own.y, top = own.y;
		auto include = [&](double x, double y, double radius)
		{
			left = std::min(left, x - radius);
			right = std::max(right, x + radius);
			bottom = std::min(bottom, y - radius);
			top = std::max(top, y + radius);
		};
		for (const auto &o : objectives)
			if (o.contains("destination_x"))
				include(o.at("destination_x"), o.at("destination_y"), o.value("radius", 0.0));
		const auto w = model_.waypoint();
		if (w.value("active", false))
			include(w.at("x"), w.at("y"), 0);
		navigation_camera_.fit(left, bottom, right, top, navigation_area_.width, navigation_area_.height);
	}
	std::vector<std::pair<std::string, std::string>> actions;
	auto button = [&](std::string key, std::string label)
	{
		actions.emplace_back(key, localise(label));
		if (key.starts_with("on-screen/"))
			return on_screen_markup(key);
		return "<button id=\"" + id(key) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
	};
	std::string markup;
	if (!mission_outcome_.empty())
	{
		std::erase_if(app.screen_controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(id(""));
		              });
		std::erase_if(app.screen_inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(id(""));
		              });
		markup =
		    "<div class=\"mission-result\" style=\"width:" + std::to_string(region.width) +
		    "px;height:" + std::to_string(region.height) + "px;\"><img class=\"mission-art\" src=\"" +
		    asset_path("mission-terminal.png") +
		    "\"/><div class=\"mission-copy\" style=\"left:" + std::to_string(region.width * .07f) +
		    "px;top:" + std::to_string(region.height * .08f) + "px;width:" + std::to_string(region.width * .86f) +
		    "px;height:" + std::to_string(region.height * .8f) + "px;\"><h1>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("title").get<std::string>()
		                                                                  : "MISSION REPORT // FINAL") +
		    "</h1><h2>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("result_title").get<std::string>()
		             : mission_outcome_ == "ftl"                          ? "Departed via FTL"
		             : mission_outcome_ == "won"                          ? "You won"
		             : mission_outcome_ == "lost"                         ? "You lost"
		                                                                  : "Draw") +
		    "</h2><div class=\"terminal-rule\"></div><p class=\"terminal-status\">" + localise("Mission complete") +
		    "</p>";
		std::string report_text = localise(mission_outcome_ == "ftl" ? "Departed via FTL" : "Mission complete");
		if (!report.is_null() && report.value("version", 1) == 2)
			report_text += ". " + localise(report.at("title")) + ". " + localise(report.at("result_title"));
		markup += "<div style=\"display:flex;gap:24px;\"><div style=\"width:68%;\">";
		for (const auto &o : report.is_null() ? objectives : report.at("objectives"))
		{
			const auto status = o.value("status", "active");
			const auto line =
			    localise(o.value("title", "")) +
			    (report.is_null() ? ""
			                      : ": " + localise((status == "active" || status == "pending") ? "Unfinished"
			                                        : status == "completed"                     ? "Completed"
			                                        : status == "failed"                        ? "Failed"
			                                                                                    : "Cancelled"));
			markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
			report_text += ". " + line;
		}
		if (!report.is_null() && report.value("version", 1) == 2)
		{
			auto line = [&](std::string value)
			{
				markup += "<p>" + Rml::StringUtilities::EncodeRml(value) + "</p>";
				report_text += ". " + value;
			};

			line(localise(report.at("explanation")));
			for (const auto &a : report.value("assessments", Json::array()))
			{
				line(localise(a.at("title")) + ": " + localise(a.at("result_title")));
				line(localise(a.at("explanation")));
			}
			markup += "<h1>" + localise("Mission log") + "</h1>";
			for (const auto &entry : report.value("log", Json::array()))
			{
				const auto seconds = entry.at("tick").get<std::uint64_t>() / 20;
				line(std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
				     std::to_string(seconds % 60) + " — " + localise(entry.at("text")) +
				     (entry.value("name", "").empty() ? "" : ": " + localise(entry.at("name"))));
			}
			markup += "</div><div style=\"width:30%;border-left:1px #51744c;padding-left:16px;\"><h1>" +
			          localise("Ship and crew") + "</h1>";
			for (const auto &part : report.value("ship_condition", Json::array()))
				line(localise(part.at("name")) + ": " + part.at("hp").dump() + "/" + part.at("maximum").dump());
			for (const auto &crew : report.value("crew", Json::array()))
				line(localise(crew.at("name")) + ": " + localise(crew.at("condition")));
			for (const auto &f : report.at("facts"))
			{
				std::string value = f.at("value").is_null()      ? localise("Unresolved")
				                    : f.at("value").is_string()  ? localise(f.at("value").get<std::string>())
				                    : f.at("value").is_boolean() ? localise(f.at("value").get<bool>() ? "Yes" : "No")
				                                                 : f.at("value").dump();
				line(localise(f.at("label")) + ": " + value);
			}
			for (const auto &p : report.at("personnel"))
				line(localise(p.at("name")) + ": " + localise(p.at("status")));
		}
		if (!report.is_null() && report.value("version", 1) == 1)
			for (const auto &name : report.at("left_behind"))
			{
				const auto line = localise("Personnel left behind") + ": " + localise(name);
				markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
				report_text += ". " + line;
			}
		markup += "</div></div>" + button("mission.leave", "Return to menu") + "</div></div>";
		app.screen_readouts.emplace_back(id("mission-result"), report_text);
	}
	else if (navigator_panel)
	{
		std::erase_if(app.screen_controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(id("game."));
		              });
		std::erase_if(app.screen_inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(id("game."));
		              });
		markup = "<div class=\"navigator-panel\" style=\"position:absolute;left:5%;top:120px;width:90%;height:" +
		         std::to_string(std::max(50.f, region.height - 160)) + "px;overflow:auto;\"><h1>" +
		         localise("FTL departure") + "</h1>";
		for (const auto &[key, text] : model_.gameplay_readouts())
			if (key.starts_with("game.readout/ftl"))
				markup += "<p>" + Rml::StringUtilities::EncodeRml(text) + "</p>";
		for (const auto &[key, label] : model_.gameplay_controls())
			if (key.starts_with("game.console/") || key.starts_with("game.control/"))
			{
				actions.emplace_back(key, label);
				markup += "<button id=\"" + id(key) + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
		if (!ftl.at(0).at("ready").get<bool>())
			markup += "<button disabled=\"disabled\" aria-disabled=\"true\">" + localise("Leave mission") + "</button>";
		markup += "</div>";
	}
	else if (navigation_visible_)
	{
		markup = "<div id=\"navigation-canvas\" style=\"position:absolute;left:0;top:110px;width:100%;height:" +
		         std::to_string(navigation_area_.height) + "px;\"></div><div class=\"navigation-controls\">";
		for (const auto &[key, label] : std::vector<std::pair<std::string, std::string>>{{"in", "Zoom in"},
		                                                                                 {"out", "Zoom out"},
		                                                                                 {"reset", "Reset view"},
		                                                                                 {"left", "Pan left"},
		                                                                                 {"right", "Pan right"},
		                                                                                 {"up", "Pan up"},
		                                                                                 {"down", "Pan down"},
		                                                                                 {"clear", "Clear waypoint"}})
			markup += button("nav." + key, label);
		if (const auto *layout = model_.layout())
			for (const auto &w : layout->at("widgets"))
				if (w.at("kind") == "navigation")
				{
					const auto key = w.at("id").get<std::string>();
					const bool active = model_.is_on_screen(key);
					markup += button("on-screen/" + key, "On screen");
					actions.back().second = localise("On screen") + ": " + localise(active ? "On" : "Off");
				}
		for (const auto &[key, label] : model_.gameplay_controls())
			if (key.starts_with("game.console/"))
				markup += button(key, label);
		markup += "<br/>X (mm) <input id=\"" + id("nav.x") + "\" value=\"" +
		          std::to_string(static_cast<std::int64_t>(navigation_x_)) + "\"/> Y (mm) <input id=\"" + id("nav.y") +
		          "\" value=\"" + std::to_string(static_cast<std::int64_t>(navigation_y_)) + "\"/>" +
		          button("nav.set", "Set waypoint") + "</div><div class=\"navigation-objectives\">";
		for (const auto &o : objectives)
			markup +=
			    (o.contains("destination_x")
			         ? button("nav.objective/" + std::to_string(o.at("id").get<std::uint64_t>()),
			                  o.value("title", "Objective"))
			         : "<p>" + Rml::StringUtilities::EncodeRml(localise(o.value("title", "Objective"))) + "</p>") +
			    "<p>" + Rml::StringUtilities::EncodeRml(localise(o.value("description", ""))) + "</p>";
		markup += "</div>";
		app.screen_inputs.push_back({id("nav.x"), localise("Waypoint X in millimetres"),
		                             std::to_string(static_cast<std::int64_t>(navigation_x_))});
		app.screen_inputs.push_back({id("nav.y"), localise("Waypoint Y in millimetres"),
		                             std::to_string(static_cast<std::int64_t>(navigation_y_))});
	}
	if (markup != navigation_markup_)
	{
		const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("navigation-layer")->SetInnerRML(markup);
		// Size the report after insertion; this layer has no intrinsic layout width.
		if (!mission_outcome_.empty())
			if (auto *copy = document_->QuerySelector(".mission-copy"))
			{
				auto *layer = document_->GetElementById("navigation-layer");
				layer->SetProperty("display", "block");
				layer->SetProperty("width", std::to_string(region.width) + "px");
				layer->SetProperty("height", std::to_string(region.height) + "px");
				auto *pane = copy->GetParentNode();
				pane->SetProperty("display", "block");
				pane->SetProperty("width", std::to_string(region.width) + "px");
				pane->SetProperty("height", std::to_string(region.height) + "px");
				copy->SetProperty("display", "block");
				copy->SetProperty("position", "absolute");
				copy->SetProperty("left", std::to_string(region.width * .07f) + "px");
				copy->SetProperty("top", std::to_string(region.height * .08f) + "px");
				copy->SetProperty("width", std::to_string(region.width * .86f) + "px");
				copy->SetProperty("height", std::to_string(region.height * .8f) + "px");
				for (int child = 0; child < copy->GetNumChildren(); ++child)
				{
					auto *line = copy->GetChild(child);
					if (line->GetTagName() == "h1" || line->GetTagName() == "h2" || line->GetTagName() == "p")
						line->SetProperty("width", std::to_string(region.width * .86f) + "px");
				}
			}

		navigation_markup_ = markup;
		elements_.clear();
		if (!mission_outcome_.empty())
		{
			app.accessible_status = mission_outcome_ == "ftl"
			                            ? localise("Departed via FTL")
			                            : localise("Mission complete") + ": " + localise(mission_outcome_);
			if (auto *button = document_->GetElementById(id("mission.leave")))
				button->Focus();
		}
		else if (auto *previous = document_->GetElementById(focus))
			previous->Focus();
	}
	for (const auto &[key, label] : actions)
		if (key.starts_with("on-screen/"))
			if (auto *b = document_->GetElementById(id(key)))
			{
				const bool active = model_.is_on_screen(key.substr(10));
				app.screen_toggles[id(key)] = active;
				b->SetClass("on-screen-active", active);
				b->SetAttribute("aria-pressed", active ? "true" : "false");
			}

	context_->Update();
	navigation_buttons_.clear();
	for (const auto &[key, label] : actions)
	{
		app.screen_controls.emplace_back(id(key), label);
		place(app, region, id(key));
		if (auto *button = document_->GetElementById(id(key)))
		{
			const auto at = button->GetAbsoluteOffset(Rml::BoxArea::Border);
			navigation_buttons_.push_back({at.x, at.y, button->GetOffsetWidth(), button->GetOffsetHeight()});
		}
	}
	if (navigation_visible_)
	{
		place(app, region, id("nav.x"));
		place(app, region, id("nav.y"));
	}
}
void draw_navigation(float ox, float oy)
{
	if (!navigation_visible_ || !mission_outcome_.empty() || settings_open_)
		return;
	if (!model_.observation())
		return;
	const auto &view = *model_.observation();
	draw_navigation_plot(
	    view.own, view.regions, model_.objectives(), model_.waypoint(), navigation_camera_,
	    {ox + navigation_area_.x, oy + navigation_area_.y, navigation_area_.width, navigation_area_.height});
}
