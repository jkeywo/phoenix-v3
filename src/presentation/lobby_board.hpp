// NativeMenuScreen::State members. The same RmlUi board is used by native and WASM lobbies.
template <class Add, class Heading, class Paragraph, class Readout>
std::string lobby_board(Application &app, Add &add, Heading &heading, Paragraph &paragraph, Readout &readout)
{
	const auto lobby = app.session->lobby();
	const auto own = std::find_if(lobby.members.begin(), lobby.members.end(),
	                              [&](const auto &member)
	                              {
		                              return member.id == lobby.self;
	                              });
	const auto controls = app.session->lobby_controls();
	std::string briefing;
	for (const auto &slot : lobby.slots)
		if (lobby.slots.size() == 1 || (own != lobby.members.end() && own->slot == slot.id))
			briefing = slot.briefing;
	if (!briefing.empty())
		app.screen_readouts.emplace_back("menu.mission-briefing", localise(briefing));
	auto allowed = [&](const std::string &id)
	{
		return std::any_of(controls.begin(), controls.end(),
		                   [&](const auto &control)
		                   {
			                   return control.first == id;
		                   });
	};
	auto control = [&](const std::string &id, const std::string &label, std::function<void()> action,
	                   bool selected = false, const std::string &short_label = "")
	{
		auto html = add(id, label, std::move(action));
		if (id.starts_with("lobby.profile/") || id.starts_with("menu.console-screen/") ||
		    id.starts_with("menu.inspector-screen/"))
			app.screen_toggles[id] = selected;
		const auto at = html.find("menu-skinned");
		html.insert(at + 12, selected ? " lobby-action menu-selected" : " lobby-action");
		if (!short_label.empty())
		{
			const auto text = Rml::StringUtilities::EncodeRml(localise(label));
			const auto start = html.find(text, html.find("menu-button-label"));
			if (start != std::string::npos)
				html.replace(start, text.size(), Rml::StringUtilities::EncodeRml(short_label));
		}
		return html;
	};
	auto act = [&](const std::string &id, const std::string &label, bool selected = false)
	{
		return control(
		    id, label,
		    [&, id]
		    {
			    app.session->session_action(id);
		    },
		    selected);
	};
	// A crew client chooses from the authoritative roster, then sees only its station.
	if (app.screen == Screen::client_lobby && (own == lobby.members.end() || own->role == JoinRole::player))
	{
		std::string html = "<div id=\"lobby-board\" class=\"lobby-board lobby-player-board\"><div id=\"lobby-player\">";
		html += heading(own != lobby.members.end() && !own->console.empty() ? "Your console" : "Choose a console");
		html += paragraph(briefing);
		if (lobby.launched)
		{
			html += readout("menu.client-connection", app.session->connection_status());
			for (const auto &[id, label] : controls)
				if (id.starts_with("network."))
					html += act(id, label);
		}
		else if (own == lobby.members.end() || !own->approved)
			html += paragraph("Connecting to the ship lobby...");
		else
			for (const auto &seat : lobby.consoles)
			{
				const bool selected = seat.member == lobby.self;
				if (!own->console.empty() && !selected)
					continue;
				html += "<div class=\"lobby-player-card\">" + heading(seat.name) + paragraph(seat.description);
				if (selected)
				{
					html += paragraph("Complexity") + "<div class=\"lobby-profile-choices\">";
					for (const auto &profile : seat.profiles)
					{
						const auto id = "lobby.profile/" + seat.slot + "/" + seat.id + "/" + profile;
						const int stars = complexity_stars(seat.profile_stars, profile);
						if (allowed(id))
						{
							if (stars)
								html += complexity_star_button(
								    act(id, "Complexity: " + std::to_string(stars) + " of 3 stars",
								        profile == seat.profile),
								    stars, complexity_stars(seat.profile_stars, seat.profile),
								    complexity_menu_assets());
							else if (seat.profile_stars.empty())
								html += act(id, profile, profile == seat.profile);
						}
					}
					html += "</div>" + paragraph(seat.ready ? "Ready. Waiting for the crew."
					                                        : "Check your Console, then ready up.");
					if (allowed(own->ready ? "lobby.unready" : "lobby.ready"))
						html += act(own->ready ? "lobby.unready" : "lobby.ready", own->ready ? "Not Ready" : "Ready");
					html += act("lobby.seat/" + seat.slot + "/", "Change console");
				}
				else if (seat.member.empty())
					html += act("lobby.seat/" + seat.slot + "/" + seat.id, "Choose this console");
				else
					html += paragraph("Claimed");
				html += "</div>";
			}
		html += "</div><div class=\"lobby-footer\"><p id=\"lobby-countdown\"></p><div>";
		html += control("menu.settings", "Settings",
		                [&]
		                {
			                app.settings_return = app.screen;
			                app.screen = Screen::settings;
		                });
		if (ai_configuration_available(app))
			html += control("ai.config", "AI configuration",
			                [&]
			                {
				                invoke_navigation(app, "ai.config");
			                });
		html += control("menu.leave", "Leave Game",
		                [&]
		                {
			                app.session->lobby_command({LobbyAction::leave});
			                app.return_home();
		                });
		return html + "</div></div></div>";
	}
	if (std::none_of(lobby.slots.begin(), lobby.slots.end(),
	                 [&](const auto &slot)
	                 {
		                 return slot.id == lobby_ship;
	                 }))
	{
		lobby_ship = own == lobby.members.end() ? "" : own->slot;
		if (lobby_ship.empty())
			for (const auto &slot : lobby.slots)
				if (!slot.host.empty())
				{
					lobby_ship = slot.id;
					break;
				}
	}
	std::string ship_name = lobby_ship;
	for (const auto &slot : lobby.slots)
		if (slot.id == lobby_ship)
		{
			ship_name = slot.ship;
			if (app.session->catalog().entities.contains(slot.ship))
				ship_name = app.session->catalog().entities.at(slot.ship).value("name", slot.ship);
		}
	const auto mission_name = lobby.scenario_name.empty() ? "Lobby" : lobby.scenario_name;
	app.screen_readouts.emplace_back("menu.lobby-mission", localise(mission_name));
	std::string html = "<div id=\"lobby-board\" class=\"lobby-board\"><div class=\"lobby-heading\">"
	                   "<h2 id=\"menu.lobby-mission\">" +
	                   Rml::StringUtilities::EncodeRml(localise(mission_name)) + "</h2>" +
	                   readout("menu.lobby-ship", ship_name) + "</div>";
	if (!briefing.empty())
		html += "<div id=\"menu.mission-briefing\" class=\"lobby-briefing\">" + paragraph(briefing) + "</div>";
	if (!lobby.code.empty())
	{
		prepare_qr(app, lobby.code);
		auto qr_button = control("menu.open-qr", "Open player invitation in a new tab",
		                         [&]
		                         {
			                         OpenURL(join_qr_url.c_str());
		                         });
		const auto label_at = qr_button.find("<span");
		qr_button.replace(label_at, qr_button.find("</span>", label_at) + 7 - label_at,
		                  "<div id=\"menu-join-qr\"></div>");
		qr_button.insert(qr_button.find("lobby-action") + 12, " lobby-qr");
		html += "<div id=\"lobby-code\" class=\"lobby-code\">" + qr_button + "<div>" +
		        readout("menu.join-code", "Join Code: " + lobby.code) +
		        control("menu.copy-code", "Copy Join Code",
		                [&]
		                {
			                copy_text(app.session->lobby().code);
		                }) +
		        "</div></div>";
	}
	auto remove_member = [&](const std::string &id, const std::string &name)
	{
		const auto action = "lobby.remove/" + id;
		return allowed(action) ? control(
		                             action, "Remove " + name,
		                             [&, action]
		                             {
			                             app.session->session_action(action);
		                             },
		                             false, localise("Remove"))
		                       : "";
	};
	const bool choosing_ship = own != lobby.members.end() && own->role == JoinRole::ship_server && own->slot.empty();
	html += "<div id=\"lobby-console-grid\">";
	if (choosing_ship)
		for (const auto &slot : lobby.slots)
			if (slot.host.empty())
			{
				html += "<div class=\"lobby-card lobby-console\">" + heading(slot.id) + paragraph("Choose a ship");
				for (const auto &ship : slot.allowed_ships)
				{
					const auto id = "lobby.ship/" + slot.id + "/" + ship;
					if (allowed(id))
						html += act(id, ship);
				}
				html += "</div>";
			}
	for (const auto &seat : lobby.consoles)
	{
		if (choosing_ship || seat.slot != lobby_ship)
			continue;
		const auto member = std::find_if(lobby.members.begin(), lobby.members.end(),
		                                 [&](const auto &m)
		                                 {
			                                 return m.id == seat.member;
		                                 });
		const auto claimant = member == lobby.members.end() ? localise("Unclaimed") : member->name;
		const auto state = member == lobby.members.end() ? "AI crew"
		                   : !member->connected          ? "Disconnected"
		                   : !member->verified           ? "Verifying content"
		                   : seat.ready                  ? "Ready"
		                                                 : "Not Ready";
		html +=
		    "<div class=\"lobby-card lobby-console\">" + heading(seat.name) +
		    "<div class=\"lobby-console-description\">" + paragraph(seat.description) + "</div>" +
		    readout("menu.console/" + seat.slot + "/" + seat.id, claimant + " / " + localise(state),
		            localise(seat.name) + ". " + localise(seat.description) + " " + claimant + " / " + localise(state));

		if (own != lobby.members.end() && own->approved && own->role == JoinRole::player &&
		    (seat.member.empty() || seat.member == lobby.self))
		{
			const auto id = "lobby.seat/" + seat.slot + "/" + seat.id;
			html += control(
			    id, (seat.member == lobby.self ? "Selected console: " : "Choose console: ") + localise(seat.name),
			    [&, id]
			    {
				    app.session->session_action(id);
			    },
			    seat.member == lobby.self,
			    localise(seat.member == lobby.self ? "Selected console" : "Choose this console"));
			if (seat.member == lobby.self && own->verified)
				html += act(own->ready ? "lobby.unready" : "lobby.ready", own->ready ? "Not Ready" : "Ready");
		}
		if (member != lobby.members.end())
			html += remove_member(member->id, member->name);
		const int stars = complexity_stars(seat.profile_stars, seat.profile);
		if (stars)
		{
			const auto rating_id = "menu.complexity/" + seat.slot + "/" + seat.id;
			html += "<div id=\"" + Rml::StringUtilities::EncodeRml(rating_id) + "\">" +
			        complexity_star_readout(stars, complexity_menu_assets()) + "</div>";
			app.screen_readouts.emplace_back(rating_id, localise("Complexity") + ": " + std::to_string(stars) + "/3");
		}
#ifndef __EMSCRIPTEN__
		if (app.can_host && lobby.coordinator && own != lobby.members.end() && seat.slot == own->slot &&
		    (seat.member.empty() || seat.local_screen))
		{
			html += "<div class=\"lobby-display-choices\">" + paragraph("Screen") + "<div>";
			for (const auto &display : app.native_displays)
				if (display.id != app.host_display)
				{
					html += control(
					    "menu.console-screen/" + seat.id + "/" + std::to_string(display.id),
					    "Claim " + localise(seat.name) + " on " + display.name,
					    [&, seat, monitor = display.id]
					    {
						    LobbyCommand command{LobbyAction::assign_local_console, "", seat.slot, "", seat.id};
						    command.screen = monitor + 2;
						    if (!app.session->lobby_command(command))
							    throw std::runtime_error("This console is already claimed");
						    std::erase_if(app.display_consoles,
						                  [&](const auto &entry)
						                  {
							                  return entry.second == seat.id;
						                  });
						    app.display_consoles[monitor] = seat.id;
						    app.local_player_names.erase(command.screen);
						    if (app.inspector_display == monitor)
							    app.inspector_display = -1;
						    if (app.apply_displays)
							    app.apply_displays();
					    },
					    seat.local_screen == static_cast<unsigned>(display.id + 2),
					    "S" + std::to_string(display.id + 1));
				}
			html += "</div></div>";
			if (seat.local_screen)
			{
				const auto route = seat.local_screen;
				if (member != lobby.members.end())
					app.local_player_names.try_emplace(route, member->name);
				html += "<div class=\"lobby-seat-actions\">" +
				        control("menu.local-ready/" + std::to_string(route), seat.ready ? "Not Ready" : "Ready",
				                [&, route, ready = seat.ready]
				                {
					                LobbyCommand command{ready ? LobbyAction::unready : LobbyAction::ready};
					                command.screen = route;
					                app.session->lobby_command(command);
				                }) +
				        control("menu.clear-console-screen/" + seat.id, "Release screen",
				                [&, seat]
				                {
					                LobbyCommand command{LobbyAction::assign_local_console};
					                command.screen = seat.local_screen;
					                if (app.session->lobby_command(command))
					                {
						                app.display_consoles.erase(static_cast<int>(seat.local_screen) - 2);
						                app.local_player_names.erase(seat.local_screen);
						                if (app.apply_displays)
							                app.apply_displays();
					                }
				                }) +
				        "</div>";
			}
		}
#endif
		html += "</div>";
	}
	html += "</div><div id=\"lobby-sidebar\"><div id=\"lobby-gm\" class=\"lobby-card\">" + heading("GM console") +
	        paragraph("Map, entities and inspection") + "<div class=\"lobby-gm-members\">";
	bool gm = app.inspector_display >= 0;
	if (gm)
		html += paragraph("Local GM / S" + std::to_string(app.inspector_display + 1));
	for (const auto &member : lobby.members)
		if (member.role == JoinRole::gm && member.connected)
		{
			html += "<div class=\"lobby-person\">" + paragraph(member.name) + remove_member(member.id, member.name) +
			        "</div>";
			gm = true;
		}
	if (!gm)
		html += paragraph("Unclaimed");
	html += "</div>";
	if (!lobby.coordinator && own != lobby.members.end() && own->role == JoinRole::gm)
		for (const auto &slot : lobby.slots)
			if (slot.id == lobby_ship && !slot.host.empty())
				html += act("lobby.seat/" + slot.id + "/", "Inspect this ship");
#ifndef __EMSCRIPTEN__
	if (app.can_host && lobby.coordinator)
	{
		html += "<div class=\"lobby-display-choices\">" + paragraph("Screen") + "<div>";
		for (const auto &display : app.native_displays)
			if (display.id != app.host_display)
				html += control(
				    "menu.inspector-screen/" + std::to_string(display.id), "Claim GM on " + display.name,
				    [&, monitor = display.id]
				    {
					    LobbyCommand command{LobbyAction::assign_local_console};
					    command.screen = monitor + 2;
					    if (!app.session->lobby_command(command))
						    throw std::runtime_error("Cannot release this screen");
					    app.display_consoles.erase(monitor);
					    app.local_player_names.erase(command.screen);
					    app.inspector_display = monitor;
					    if (app.apply_displays)
						    app.apply_displays();
				    },
				    app.inspector_display == display.id, "S" + std::to_string(display.id + 1));
		html += "</div></div>";
		if (app.inspector_display >= 0)
			html += control("menu.clear-inspector-screen", "Release screen",
			                [&]
			                {
				                app.inspector_display = -1;
				                if (app.apply_displays)
					                app.apply_displays();
			                });
	}
#endif
	html += "</div><div id=\"lobby-ships\" class=\"lobby-card\">" + heading("Other ships");
	bool ships = false;
	for (const auto &slot : lobby.slots)
		if (slot.id != lobby_ship && !slot.host.empty())
		{
			ships = true;
			std::size_t filled = 0, maximum = 0;
			for (const auto &seat : lobby.consoles)
				if (seat.slot == slot.id)
				{
					++maximum;
					if (std::any_of(lobby.members.begin(), lobby.members.end(),
					                [&](const auto &member)
					                {
						                return member.id == seat.member && member.connected;
					                }))
						++filled;
				}
			html += "<div class=\"lobby-person\">" +
			        control("menu.lobby-ship/" + slot.id,
			                slot.ship + " (" + std::to_string(filled) + "/" + std::to_string(maximum) + ")",
			                [&, slot]
			                {
				                lobby_ship = slot.id;
			                }) +
			        remove_member(slot.host, slot.ship) + "</div>";
		}

	if (!ships)
		html += paragraph("No other ships connected");
	html += "</div><div id=\"lobby-waiting\" class=\"lobby-card\">" + heading("Awaiting console");
	bool waiting = false;
	for (const auto &member : lobby.members)
		if (member.role == JoinRole::player && member.console.empty() && member.connected)
		{
			waiting = true;
			html += "<div class=\"lobby-person\">" + readout("menu.member/" + member.id, member.name) +
			        remove_member(member.id, member.name) + "</div>";
		}
	if (!waiting)
		html += paragraph("Everyone has a console");
	html += "</div></div><div class=\"lobby-footer\"><p id=\"lobby-countdown\"></p><div>";
	if (own != lobby.members.end() && (own->console.empty() || own->role == JoinRole::gm))
		for (const auto &[id, label] : controls)
			if (id == "lobby.ready" || id == "lobby.unready")
				html += act(id, label);
	html += control("menu.settings", "Settings",
	                [&]
	                {
		                app.settings_return = app.screen;
		                app.screen = Screen::settings;
		                focus_next = "settings.tab/" + app.settings_tab;
	                });
	if (ai_configuration_available(app))
		html += control("ai.config", "AI configuration",
		                [&]
		                {
			                invoke_navigation(app, "ai.config");
		                });
	html += control("menu.leave", "Leave Game",
	                [&]
	                {
		                app.session->lobby_command({LobbyAction::leave});
		                app.return_home();
	                }) +
	        "</div></div></div>";
	return html;
}
void layout_lobby(const Application &app, Rml::ElementDocument &document, const ConsoleRegion &region)
{
	const bool visible = document.GetElementById("lobby-board") != nullptr;
	if (auto *title = document.GetElementById("menu-title"))
		title->SetProperty("display", visible ? "none" : "block");
	if (!visible || document.GetElementById("lobby-player"))
		return;
	const float sidebar = std::clamp(region.width * .23f, 205.f, 290.f);
	const float gap = 12;
	const float width = std::max(1.f, region.width - sidebar - 3 * gap - 24);
	auto box = [&](Rml::Element *element, float x, float y, float w, float h)
	{
		element->SetProperty("left", std::to_string(x) + "dp");
		element->SetProperty("top", std::to_string(y) + "dp");
		element->SetProperty("width", std::to_string(w) + "dp");
		element->SetProperty("height", std::to_string(h) + "dp");
	};
	float grid_top = 126;
	if (auto *briefing = document.GetElementById("menu.mission-briefing"))
	{
		const float briefing_height = std::clamp(region.height * .18f, 100.f, 160.f);
		box(briefing, 24, 110, width, briefing_height);
		grid_top = 110 + briefing_height + gap;
	}
	if (auto *heading = document.QuerySelector(".lobby-heading"))
		heading->SetProperty("width", std::to_string(width) + "dp");
	const float height = std::max(1.f, region.height - grid_top - 84);
	box(document.GetElementById("lobby-console-grid"), 24, grid_top, width, height);
	Rml::ElementList cards;
	document.GetElementById("lobby-console-grid")->GetElementsByClassName(cards, "lobby-console");
	const int count = static_cast<int>(cards.size());
	const int columns = std::max(1, std::min({4, static_cast<int>((width + gap) / 205), (count + 1) / 2}));
	const int rows = std::max(1, (count + columns - 1) / columns);
	const float card_width = (width - (columns - 1) * gap) / columns;
	const float card_height = (height - (rows - 1) * gap) / rows;
	for (int i = 0; i < count; ++i)
	{
		cards[i]->SetClass("lobby-compact", card_height < 240 || card_width < 265);
		box(cards[i], (i % columns) * (card_width + gap), (i / columns) * (card_height + gap), card_width, card_height);
	}
	document.GetElementById("lobby-gm")->SetClass("lobby-compact", sidebar < 240);
	const float sidebar_top = document.GetElementById("lobby-code") ? sidebar + 112 : 126;
	if (auto *code = document.GetElementById("lobby-code"))
		box(code, region.width - sidebar - 24, 20, sidebar, sidebar + 80);
	if (auto *qr = document.GetElementById("menu.open-qr"))
		qr->SetProperty("height", std::to_string(sidebar) + "dp");
	const float sidebar_height = std::max(410.f, region.height - sidebar_top - 84);
	box(document.GetElementById("lobby-sidebar"), region.width - sidebar - 24, sidebar_top, sidebar,
	    std::max(1.f, region.height - sidebar_top - 84));
	const float gm_height = 190.f;
	const float remainder = sidebar_height - gm_height - 2 * gap;
	const auto lobby = app.session->lobby();
	const auto ships = std::count_if(lobby.slots.begin(), lobby.slots.end(),
	                                 [&](const auto &slot)
	                                 {
		                                 return slot.id != lobby_ship && !slot.host.empty();
	                                 });
	const auto waiting =
	    std::count_if(lobby.members.begin(), lobby.members.end(),
	                  [](const auto &member)
	                  {
		                  return member.role == JoinRole::player && member.console.empty() && member.connected;
	                  });
	const float ships_height = std::clamp(70.f + ships * 42.f, std::min(94.f, remainder * .5f),
	                                      std::max(94.f, remainder - 68.f - waiting * 23.f));
	box(document.GetElementById("lobby-gm"), 0, 0, sidebar, gm_height);
	box(document.GetElementById("lobby-ships"), 0, gm_height + gap, sidebar, ships_height);
	box(document.GetElementById("lobby-waiting"), 0, gm_height + 2 * gap + ships_height, sidebar,
	    remainder - ships_height);
}
