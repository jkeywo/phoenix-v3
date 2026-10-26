#pragma once
struct PadSurface
{
	PadNavigation navigation;
	std::string device, identity;
	ConsoleId route = 0;
	bool ui = true, flight = false;
	Screen screen = Screen::home;
	std::weak_ptr<SessionPort> session;
	std::uint64_t generation = 0;
};
struct PadSelection
{
	std::string selected, held, holder, editing;
	double pending = 0;
};
std::map<std::string, PadSurface> pad_surfaces;
std::map<ConsoleId, PadSelection> pad_selections;
Rml::Element *pad_element(ConsoleId route, const std::string &id)
{
	if (!route && id.starts_with("console/"))
	{
		const auto end = id.find('/', 8);
		if (end != std::string::npos)
			route = std::stoull(id.substr(8, end - 8));
	}
	if (route && console_surfaces.contains(route))
		return console_surfaces.at(route)->controller_element(id);
	return document->GetElementById(id);
}
void release_pad(ConsoleId route)
{
	auto &selection = pad_selections[route];
	if (auto *element = pad_element(route, selection.selected))
		element->SetClass("pad-selected", false);
	if (console_surfaces.contains(route))
	{
		if (!selection.held.empty())
			console_surfaces.at(route)->controller_hold(selection.held, false);
		console_surfaces.at(route)->controller_cancel();
	}
	selection.held.clear();
	selection.holder.clear();
	selection.editing.clear();
}
void cancel_pad(const std::string &device)
{
	if (auto it = pad_surfaces.find(device); it != pad_surfaces.end())
	{
		release_pad(it->second.route);
		it->second.navigation.cancel();
		pad_surfaces.erase(it);
	}
}
void prepare_pad(const std::string &device, ConsoleId route, const Application &app)
{
	auto &pad = pad_surfaces[device];
	pad.device = device;
	const bool flight = console_surfaces.contains(route) && console_surfaces.at(route)->controller_flight() &&
	                    app.screen != Screen::settings;
	const auto identity =
	    console_surfaces.contains(route) ? console_surfaces.at(route)->controller_identity() : std::string{};
	const auto generation = app.session ? app.session->generation() : 0;
	if (pad.route != route || pad.screen != app.screen || pad.session.lock() != app.session ||
	    pad.generation != generation || pad.flight != flight || pad.identity != identity)
	{
		release_pad(pad.route);
		input.assign_gamepad(device, {});
		pad.navigation.cancel();
		pad.route = route;
		pad.screen = app.screen;
		pad.session = app.session;
		pad.generation = generation;
		pad.flight = flight;
		pad.identity = identity;
		pad.ui = !flight;
		pad_selections[route].selected.clear();
	}
}
std::vector<SemanticControl> pad_controls(const Application &app, ConsoleId route, const std::string &device)
{
	auto controls = navigation_controls(app);
	const auto prefix = "console/" + std::to_string(route) + "/";
	std::erase_if(controls,
	              [&](const auto &control)
	              {
		              if (control.read_only || !control.placement.enabled ||
		                  (route ? !control.id.starts_with(prefix)
		                         : (control.id.starts_with("console/") && !control.id.ends_with("/pad/" + device))))
			              return true;
		              auto *element = pad_element(route, control.id);
		              const auto *field = dynamic_cast<Rml::ElementFormControl *>(element);
		              return !element || !element->IsVisible(true) || (field && field->IsDisabled());
	              });
	const auto order = [&](const SemanticControl &control)
	{
		std::vector<std::uint64_t> path;
		for (auto *node = pad_element(route, control.id); node && node->GetParentNode(); node = node->GetParentNode())
		{
			auto *parent = node->GetParentNode();
			for (int i = 0; i < parent->GetNumChildren(); ++i)
				if (parent->GetChild(i) == node)
				{
					path.push_back(i);
					break;
				}
		}
		std::reverse(path.begin(), path.end());
		const auto document_route =
		    control.id.starts_with("console/") ? std::stoull(control.id.substr(8, control.id.find('/', 8) - 8)) : 0;
		path.insert(path.begin(), document_route);
		return path;
	};
	std::map<std::string, std::vector<std::uint64_t>> orders;
	for (const auto &control : controls)
		orders.emplace(control.id, order(control));
	std::stable_sort(controls.begin(), controls.end(),
	                 [&](const auto &a, const auto &b)
	                 {
		                 return orders.at(a.id) < orders.at(b.id);
	                 });
	return controls;
}
void pad_select(PadSelection &selection, ConsoleId route, const SemanticControl &control, Application &app)
{
	if (auto *old = pad_element(route, selection.selected))
		old->SetClass("pad-selected", false);
	selection.selected = control.id;
	if (auto *element = pad_element(route, control.id))
		element->SetClass("pad-selected", true);
	if (auto *element = pad_element(route, control.id))
		element->ScrollIntoView({Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
		                         Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
	app.accessible_status = control.label;
}
void pad_action(Application &app, PadSurface &pad, const PadNavigationEvent &event)
{
	auto &selection = pad_selections[pad.route];
	if (!event.pressed)
	{
		if (event.action == "activate" && !selection.held.empty() && selection.holder == pad.device)
		{
			if (console_surfaces.contains(pad.route))
				console_surfaces.at(pad.route)->controller_hold(selection.held, false);
			selection.held.clear();
		}
		return;
	}
	if (event.action == "mode" && pad.flight)
	{
		release_pad(pad.route);
		pad.ui = !pad.ui;
		pad.navigation.cancel();
		for (const auto &[device, candidate] : pad_surfaces)
			if (&candidate == &pad)
				input.assign_gamepad(device, {});
		app.accessible_status = localise(pad.ui ? "Controller: UI navigation" : "Controller: Flight");
		return;
	}
	if (!pad.ui)
		return;
	if (event.action == "settings")
	{
		release_pad(pad.route);
		invoke_navigation(app, pad.route ? "console/" + std::to_string(pad.route) + "/settings" : "settings");
		return;
	}
	if (event.action == "cancel")
	{
		if (!selection.editing.empty())
		{
			selection.editing.clear();
			return;
		}
		release_pad(pad.route);
		if (console_surfaces.contains(pad.route) && console_surfaces.at(pad.route)->controller_back())
			return;
		if (app.screen == Screen::settings)
		{
			invoke_navigation(app, "settings.close");
			return;
		}
		for (const auto &control : navigation_controls(app))
			if (control.id == "menu.back")
			{
				invoke_navigation(app, "menu.back");
				return;
			}
		context->ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		context->ProcessKeyUp(Rml::Input::KI_ESCAPE, 0);
		return;
	}
	auto controls = pad_controls(app, pad.route, pad.device);
	if (controls.empty())
	{
		release_pad(pad.route);
		selection.selected.clear();
		return;
	}
	auto current = std::find_if(controls.begin(), controls.end(),
	                            [&](const auto &c)
	                            {
		                            return c.id == selection.selected;
	                            });
	if (current == controls.end())
	{
		release_pad(pad.route);
		pad_select(selection, pad.route, controls.front(), app);
		current = controls.begin();
	}
	if (!selection.editing.empty() && selection.editing != current->id)
		selection.editing.clear();
	if (event.action == "activate")
	{
		if (!selection.held.empty())
			return;
		if (current->editable)
		{
			if (current->numeric)
			{
				if (selection.editing == current->id)
				{
					if (app.set_control_value)
						app.set_control_value(current->id,
						                      std::to_string(static_cast<long long>(std::llround(selection.pending))));
					selection.editing.clear();
				}
				else
				{
					selection.editing = current->id;
					try
					{
						selection.pending = std::stod(current->value);
					}
					catch (...)
					{
						selection.pending = 0;
					}
				}
			}
			else if (app.focus_control)
			{
				app.focus_control(current->id);
				focus_semantic_text(current->id);
				app.accessible_status = localise("Use the keyboard to enter text");
			}
		}
		else if (console_surfaces.contains(pad.route) &&
		         console_surfaces.at(pad.route)->controller_hold(current->id, true))
		{
			selection.held = current->id;
			selection.holder = pad.device;
		}
		else
			invoke_navigation(app, current->id);
		return;
	}
	const int dx = event.action == "left" ? -1 : event.action == "right" ? 1 : 0;
	const int dy = event.action == "up" ? -1 : event.action == "down" ? 1 : 0;
	if (!selection.editing.empty() && (dx || dy))
	{
		selection.pending =
		    std::clamp(selection.pending + (dx - dy) * current->step, current->minimum, current->maximum);
		app.accessible_status = current->label + ": " + std::to_string(selection.pending);
		return;
	}
	if (event.action == "previous" || event.action == "next")
	{
		const auto index =
		    (current - controls.begin() + controls.size() + (event.action == "next" ? 1 : -1)) % controls.size();
		pad_select(selection, pad.route, controls[index], app);
	}
	else if (dx || dy)
	{
		const auto &a = current->placement;
		double best = 1e30;
		const SemanticControl *next = nullptr;
		for (const auto &candidate : controls)
		{
			const auto &b = candidate.placement;
			const double x = b.x + b.width / 2 - a.x - a.width / 2, y = b.y + b.height / 2 - a.y - a.height / 2;
			if (x * dx + y * dy <= 1)
				continue;
			const double score = std::hypot(x, y) + 2 * std::abs(dx ? y : x);
			if (score < best)
			{
				best = score;
				next = &candidate;
			}
		}
		if (next)
			pad_select(selection, pad.route, *next, app);
	}
}
void update_pads(Application &app)
{
	for (auto &[device, pad] : pad_surfaces)
	{
		if (!window_focused() || app.screen == Screen::workshop || app.screen == Screen::gm ||
		    (pad.route &&
		     (!app.gamepad_assignments.contains(device) || app.gamepad_assignments.at(device) != pad.route)))
		{
			release_pad(pad.route);
			pad.navigation.cancel();
			continue;
		}
		for (const auto &event : pad.navigation.drain(GetTime()))
			pad_action(app, pad, event);
		const auto controls = pad_controls(app, pad.route, pad.device);
		auto &selection = pad_selections[pad.route];
		const auto available = [&](const std::string &id)
		{
			return std::any_of(controls.begin(), controls.end(),
			                   [&](const auto &c)
			                   {
				                   return c.id == id;
			                   });
		};
		if ((!selection.held.empty() && !available(selection.held)) ||
		    (!selection.editing.empty() && !available(selection.editing)))
			release_pad(pad.route);
		if (!selection.selected.empty() && !available(selection.selected))
			selection.selected.clear();
	}
}
void draw_pad_selection(Application &app)
{
	for (const auto &[device, pad] : pad_surfaces)
	{
		if (!window_focused())
			continue;
		float badge_x = 8, badge_y = static_cast<float>(GetScreenHeight()) - 24;
		for (const auto &r : app.console_regions)
			if (r.console == pad.route)
			{
				badge_x = r.x + 8;
				badge_y = r.y + r.height - 24;
			}
		DrawText(localise(pad.ui ? "UI navigation | Select: mode | A: activate | B: back | LB/RB: previous/next"
		                         : "Flight | Select: UI navigation")
		             .c_str(),
		         static_cast<int>(badge_x), static_cast<int>(badge_y), 16, ORANGE);
		const auto &selection = pad_selections[pad.route];
		const auto controls = pad_controls(app, pad.route, pad.device);
		for (const auto &control : controls)
			if (pad.ui && control.id == selection.selected)
				if (auto *element = pad_element(pad.route, control.id))
				{
					const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
					float x = 0, y = 0;
					for (const auto &r : displayed_regions)
						if (r.console == (control.id.starts_with("console/")
						                      ? std::stoull(control.id.substr(8, control.id.find('/', 8) - 8))
						                      : 0))
						{
							x = r.x;
							y = r.y;
						}
					if (element->IsClassSet("radar-contact"))
					{
						const auto c = element->GetProperty<Rml::Colourb>("image-color");
						draw_radar_selection({x + offset.x + element->GetOffsetWidth() / 2,
						                      y + offset.y + element->GetOffsetHeight() / 2},
						                     16, {c.red, c.green, c.blue, c.alpha});
					}
					else
						DrawRectangleLinesEx(
						    {x + offset.x, y + offset.y, element->GetOffsetWidth(), element->GetOffsetHeight()}, 3,
						    ORANGE);
					if (!selection.editing.empty())
						DrawText(std::to_string(selection.pending).c_str(), static_cast<int>(x + offset.x),
						         static_cast<int>(y + offset.y), 20, ORANGE);
				}
	}
}
