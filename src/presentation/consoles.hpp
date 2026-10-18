#pragma once
namespace phoenix
{
class ConsoleSurface final : public Rml::EventListener
{
	ConsoleId route_;
	std::string context_name_;
	bool toolbar_enabled_ = true;
	Rml::Context *context_ = nullptr;
	Rml::ElementDocument *document_ = nullptr;
	ConsoleModel model_;
	TextureCache radar_textures_;
	Application *app_ = nullptr;
	std::string signature_;
	std::map<std::string, std::string> displayed_values_;
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0;
	std::vector<std::pair<std::string, std::string>> controls_;
	std::map<std::string, Texture2D> hud_;
	std::map<std::string, Json> hud_metadata_;
	std::optional<std::array<float, 3>> heading_dial_;
	std::set<std::uint64_t> heading_contacts_;
	std::string asset_path(const std::string &name) const
	{
#ifdef __EMSCRIPTEN__
		return "/assets/helm/" + name;
#else
		return std::string(GetApplicationDirectory()) + "assets/helm/" + name;
#endif
	}
	Texture2D hud(const std::string &name)
	{
		if (!hud_.contains(name))
		{
			const auto texture = LoadTexture(asset_path(name + ".png").c_str());
			if (!texture.id)
				throw std::runtime_error("Missing Console artwork: " + name);
			hud_[name] = texture;
		}
		return hud_.at(name);
	}
	static void image(Texture2D texture, Rectangle area)
	{
		DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)}, area,
		               {0, 0}, 0, WHITE);
	}
	Vector2 widget_offset(const std::string &key) const
	{
		const auto at = document_->GetElementById("widget/" + key)->GetAbsoluteOffset(Rml::BoxArea::Border);
		return {at.x, at.y};
	}
	Rectangle throttle_rect(const std::string &key) const
	{
		auto *widget = document_->GetElementById("widget/" + key);
		const auto at = widget_offset(key);
		const auto &size = hud_metadata_.at("helm-throttle-track").at("image_size");
		const float height = std::max(30.f, widget->GetOffsetHeight() - 100),
		            width = height * size[0].get<float>() / size[1].get<float>();
		return {at.x + (widget->GetOffsetWidth() - width) / 2, at.y + 45, width, height};
	}
	void place(Application &app, const ConsoleRegion &region, const std::string &name)
	{
		if (auto *element = document_->GetElementById(name))
		{
			const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
			app.control_placements[name] = {region.x + at.x,
			                                region.y + at.y,
			                                element->GetOffsetWidth(),
			                                element->GetOffsetHeight(),
			                                context_->GetFocusElement() == element,
			                                true};
		}
	}
	static std::string escaped(const std::string &value)
	{
		std::string text;
		for (char c : value)
		{
			if (c == '&')
				text += "&amp;";
			else if (c == '<')
				text += "&lt;";
			else if (c == '>')
				text += "&gt;";
			else if (c == '"')
				text += "&quot;";
			else
				text += c;
		}
		return text;
	}
	std::string id(const std::string &action) const
	{
		return "console/" + std::to_string(route_) + "/" + action;
	}
	std::string button(const std::string &action, const std::string &label)
	{
		controls_.emplace_back(id(action), label);
		return "<button id=\"" + escaped(id(action)) + "\">" + escaped(translate(label, app_->pseudo_locale)) +
		       "</button>";
	}

public:
	explicit ConsoleSurface(ConsoleId route, const std::string &prefix = "console-", bool toolbar = true)
	    : route_(route), context_name_(prefix + std::to_string(route)), toolbar_enabled_(toolbar)
	{
		context_ = Rml::CreateContext(context_name_, {1280, 720});
		if (!context_)
			throw std::runtime_error("Cannot create independent Console context");
		document_ = context_->LoadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#e5e4ca;}
h1 {font-size:26px;display:inline-block;margin:0 18px 8px 0;}
h2 {font-size:20px;display:block;margin:4px 0;}
button {display:inline-block;padding:10px;margin:4px;border:1px #9c8b68;background:#222b2b;color:#f0e3c2;}
button:hover,button:focus {background:#455458;border-color:#ec9c52;}
#toolbar {position:absolute;left:5%;top:4%;right:5%;height:85px;overflow:auto;}
#connection-status {font-size:15px;}
#widgets {position:absolute;left:4%;right:4%;top:125px;bottom:4%;}
.widget {position:absolute;box-sizing:border-box;padding:8px;overflow:hidden;}
p {display:block;font-size:17px;margin:4px 0;}
.dial {position:absolute;left:12px;right:12px;top:48px;bottom:72px;}
.widget button {position:absolute;bottom:8px;left:8px;}
.readout {position:absolute;bottom:12px;left:16px;right:16px;height:44px;text-align:center;color:#cbe9c8;}
.heading-value {display:inline-block;vertical-align:middle;margin:0 8px;width:145px;height:36px;background:#0a1719;color:#cbe9c8;border:1px #988b71;font-size:26px;}
.throttle {position:absolute;background:transparent;border:0px;}
.throttle slidertrack {background:transparent;}
.throttle sliderbar {width:100%;background:transparent;}
.heading-value:focus {border-color:#ffb665;background:#122629;}
.throttle sliderarrowdec,.throttle sliderarrowinc {display:none;}
.ready-card {position:absolute;left:17%;top:32%;width:66%;box-sizing:border-box;padding:28px;background:#0b1719ee;border:2px #d48a48;}
.ready-card h1 {display:block;font-size:32px;}
.ready-card button {min-width:120px;}
</style></head><body><div id="toolbar"></div><div id="widgets"></div><div id="lobby"></div></body></rml>)");
		if (!document_)
			throw std::runtime_error("Cannot create Console document");
		document_->AddEventListener("click", this);
		document_->AddEventListener("change", this);
		for (const char *name :
		     {"helm-throttle-track", "helm-throttle-handle", "helm-radar-ring", "helm-background-9slice"})
			hud_metadata_[name] =
			    parse_json(read_file(asset_path(std::string(name) + ".sprite.json"), 64 * 1024), name);
		document_->Show();
	}
	~ConsoleSurface()
	{
		for (const auto &[name, texture] : hud_)
			UnloadTexture(texture);
		Rml::RemoveContext(context_name_);
	}
	Rml::Context &context()
	{
		return *context_;
	}
	bool action(const std::string &name)
	{
		const auto prefix = id("");
		if (!app_ || !name.starts_with(prefix))
			return false;
		const auto action = name.substr(prefix.size());
		if ((app_->screen != Screen::console && app_->screen != Screen::viewscreen &&
		     app_->screen != Screen::server_lobby) ||
		    owner_.lock() != app_->session || (app_->session && generation_ != app_->session->generation()))
			return false;
		if (action == "ready" || action == "back")
		{
			const auto lobby = app_->session->lobby();
			if (!lobby.active || lobby.launched)
				return false;
			LobbyCommand command{LobbyAction::assign_local_console};
			command.screen = route_;
			if (action == "ready")
				for (const auto &seat : lobby.consoles)
					if (seat.local_screen == route_)
						command.action = seat.ready ? LobbyAction::unready : LobbyAction::ready;
			const bool accepted = app_->session->lobby_command(command);
			if (accepted && action == "back")
			{
				app_->display_consoles.erase(static_cast<int>(route_) - 2);
				if (app_->apply_displays)
					app_->apply_displays();
			}
			return accepted;
		}
		if (action == "settings")
			return invoke_navigation(*app_, "settings");
		if (action == "release")
			return app_->session && app_->session->release_console(route_);
		if (action.starts_with("claim/"))
		{
			for (const auto &seat : app_->session->console_seats())
				if (action == "claim/" + std::to_string(seat.address.ship) + "/" + seat.address.console)
					return app_->session->claim_console(route_, seat.address);
			return false;
		}
		if (action.starts_with("pad/"))
		{
			app_->gamepad_assignments[action.substr(4)] = route_;
			return true;
		}
		const bool accepted = model_.act(action);
		app_->accessible_status = model_.announcement();
		return accepted;
	}
	void ProcessEvent(Rml::Event &event) override
	{
		const auto name = event.GetTargetElement()->GetId();
		if (event.GetType() == "change")
		{
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(event.GetTargetElement()))
			{
				if (name.starts_with(id("speed/")))
				{
					double value = 0;
					const auto text = field->GetValue();
					if (std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{})
						set_value(name, std::to_string(-value));
				}
				else
					set_value(name, field->GetValue());
			}
			return;
		}
		action(name);
	}
	void focus(const std::string &name)
	{
		if (auto *element = document_->GetElementById(name))
			element->Focus(true);
	}
	bool set_value(const std::string &name, const std::string &text)
	{
		if (!app_ || !name.starts_with(id("")) || !game_active(*app_) || owner_.lock() != app_->session ||
		    generation_ != app_->session->generation())
			return false;
		double value = 0;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
			return false;
		const auto action = name.substr(id("").size());
		bool accepted = false;
		if (action.starts_with("speed/"))
			accepted = model_.set_speed_fraction(value / 100.0);
		else if (action.starts_with("heading/"))
			accepted = model_.set_heading_degrees(value);
		if (accepted)
		{
			app_->accessible_status = model_.announcement();
			if (action.starts_with("heading/"))
				if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(name));
				    field && field->GetValue() != text)
					field->SetValue(text);
		}
		return accepted;
	}
	bool pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
	{
		if (!app_ || !game_active(*app_) || !heading_dial_ || owner_.lock() != app_->session ||
		    generation_ != app_->session->generation())
			return false;
		const auto [cx, cy, radius] = *heading_dial_;
		const float dx = x - cx, dy = cy - y, length = std::hypot(dx, dy);
		if (kind == DeviceEventKind::Down)
		{
			if (length < radius * .69f || length > radius || heading_contacts_.size())
				return false;
			heading_contacts_.insert(contact);
		}
		if (!heading_contacts_.contains(contact))
			return false;
		if (kind != DeviceEventKind::Cancel && length > 1)
		{
			model_.set_heading_degrees(std::fmod(std::atan2(dy, dx) * 180.0 / 3.141592653589793 + 360.0, 360.0));
			app_->accessible_status = model_.announcement();
		}
		if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			heading_contacts_.erase(contact);
		return true;
	}
	void cancel_pointer()
	{
		heading_contacts_.clear();
	}
	bool probe_helm()
	{
		if (!heading_dial_ || !model_.layout())
			return false;
		bool throttle = false;
		for (const auto &widget : model_.layout()->at("widgets"))
			if (widget.at("kind") == "throttle")
			{
				auto *element = document_->GetElementById(id("speed/" + widget.at("id").get<std::string>()));
				const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
				context_->ProcessMouseMove(static_cast<int>(at.x + element->GetOffsetWidth() / 2),
				                           static_cast<int>(at.y + element->GetOffsetHeight() * .25f), 0);
				context_->ProcessMouseButtonDown(0, 0);
				context_->ProcessMouseButtonUp(0, 0);
				throttle = model_.requested_speed() > 0;
			}
		const auto [x, y, r] = *heading_dial_;
		return throttle && pointer(999, x, y - r * .85f, DeviceEventKind::Down) &&
		       pointer(999, x, y - r * .85f, DeviceEventKind::Up) && model_.requested_heading() == 90000;
	}
	void prepare(Application &app, const ConsoleRegion &region, const std::set<std::string> &pads)
	{
		app_ = &app;
		model_.refresh(app.session, route_);
		if (owner_.lock() != app.session || (app.session && generation_ != app.session->generation()))
		{
			owner_ = app.session;
			generation_ = app.session ? app.session->generation() : 0;
			radar_textures_.clear();
			signature_.clear();
			cancel_pointer();
			document_->Focus();
		}
		context_->SetDimensions({static_cast<int>(region.width), static_cast<int>(region.height)});
		const bool pregame = app.session && app.session->lobby().active && !app.session->lobby().launched;
		const Json *layout = model_.layout();
		std::string title = "Console", description;
		bool ready = false;
		if (pregame)
		{
			const auto lobby = app.session->lobby();
			for (const auto &seat : lobby.consoles)
				if (seat.local_screen == route_)
				{
					title = seat.name;
					ready = seat.ready;
					for (const auto &slot : lobby.slots)
						if (slot.id == seat.slot)
							for (const auto &spec : resolve_consoles(app.session->catalog(), slot.ship))
								if (spec.id == seat.id)
									layout = &app.session->catalog().consoles.at(spec.definition);
				}
		}
		if (layout)
		{
			title = layout->value("name", layout->value("id", title));
			description =
			    layout->value("description",
			                  std::string("Operate the fitted systems and monitor this Console's permitted contacts."));
		}
		// Authored widgets still determine controls, placement and authority.
		std::string signature = (layout ? layout->dump() : "vacant") + (app.pseudo_locale ? "pseudo" : "normal") +
		                        (pregame ? ready ? "ready" : "waiting" : "running");
		if (model_.observation())
			signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		for (const auto &pad : pads)
			signature += "/pad/" + pad;
		if (signature != signature_)
		{
			signature_ = std::move(signature);
			displayed_values_.clear();
			controls_.clear();
			heading_contacts_.clear();
			std::string toolbar = "<h1>" + escaped(localise(title)) + "</h1>";
			if (toolbar_enabled_ && !pregame)
				toolbar += button("settings", "Settings");
			for (const auto &pad : toolbar_enabled_ ? pads : std::set<std::string>{})
				toolbar += button("pad/" + pad, "Assign controller " + pad);
			toolbar += "<p id=\"connection-status\"></p>";
			document_->GetElementById("toolbar")->SetInnerRML(toolbar);
			std::string widgets;
			bool has_radar = false;
			if (layout)
				for (const auto &widget : layout->at("widgets"))
					if (widget.at("kind") == "radar")
						has_radar = true;
			if (layout)
				for (const auto &widget : layout->at("widgets"))
				{
					const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
					auto percent = [&](const char *field)
					{
						return std::to_string(widget.at(field).get<double>() / 10) + "%";
					};
					widgets += "<div class=\"widget\" id=\"widget/" + escaped(key) + "\" style=\"left:" + percent("x") +
					           ";top:" + percent("y") + ";width:" + percent("width") + ";height:" + percent("height") +
					           "\"><h2>" + escaped(localise(widget.at("label"))) + "</h2>";
					if (kind == "throttle")
						widgets += "<input class=\"throttle\" id=\"" + id("speed/" + key) +
						           "\" type=\"range\" orientation=\"vertical\" min=\"-100\" max=\"0\" step=\"0.1\" "
						           "value=\"0\"" +
						           (pregame ? " disabled" : "") + "/>";
					if (kind == "heading")
					{
						widgets += "<label for=\"" + id("heading/" + key) + "\">" +
						           escaped(localise("Desired heading (degrees)")) +
						           "</label><input class=\"heading-value\" id=\"" + id("heading/" + key) +
						           "\" type=\"text\" maxlength=\"7\" value=\"0\"" + (pregame ? " disabled" : "") +
						           "/><p>" + escaped(localise("Click or drag the radar ring. East 0; North 90.")) +
						           "</p>";
						if (!has_radar)
							widgets += "<div class=\"dial\" id=\"dial/" + escaped(key) + "\"/>";
					}
					if (kind == "radar")
						widgets += "<div class=\"dial\" id=\"dial/" + escaped(key) + "\"/>" +
						           button("game.contacts/" + key, "Read radar contacts");
					widgets += "<p class=\"" + std::string(kind == "throttle" ? "readout" : "heading-actual") +
					           "\" id=\"value/" + escaped(key) + "\"/></div>";
				}
			document_->GetElementById("widgets")->SetInnerRML(widgets);
			std::string lobby;
			if (pregame)
				lobby =
				    "<div class=\"ready-card\"><h1 id=\"" + id("lobby-title") + "\">" + escaped(localise(title)) +
				    "</h1><p>" + escaped(localise(description)) + "</p><p>" +
				    escaped(localise(ready ? "Ready. Waiting for the crew." : "Check your Console, then ready up.")) +
				    "</p>" + button("ready", ready ? "Not Ready" : "Ready") + button("back", "Back") +
				    "<p id=\"countdown\"/></div>";
			document_->GetElementById("lobby")->SetInnerRML(lobby);
		}
		if (pregame)
			app.screen_readouts.emplace_back(id("lobby-title"), localise(title) + ". " + localise(description));
		if (auto *status = document_->GetElementById("connection-status"))
			status->SetInnerRML(escaped(localise(pregame ? description : model_.announcement())));
		if (pregame)
			if (auto *countdown = document_->GetElementById("countdown"))
			{
				const auto left = app.session->lobby().countdown_ms;
				countdown->SetInnerRML(
				    left < 0 ? "" : escaped(localise("Starting in")) + " " + std::to_string((left + 999) / 1000));
			}
		context_->Update();
		heading_dial_.reset();
		bool can_turn = false;
		if (layout)
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("kind") == "heading")
					can_turn = true;
		if (layout)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
				if (kind == "radar" || (kind == "heading" && !heading_dial_))
					if (auto *dial = document_->GetElementById("dial/" + key))
					{
						const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
						const float radius =
						    std::max(1.f, std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f);
						if (can_turn)
							heading_dial_ = {at.x + dial->GetOffsetWidth() / 2, at.y + dial->GetOffsetHeight() / 2,
							                 radius};
					}
				if (kind == "throttle")
				{
					const auto track = throttle_rect(key);
					const auto &markers = hud_metadata_.at("helm-throttle-track").at("markers");
					const float top = markers.at("travel_max")[1].get<float>() /
					                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
					            bottom = markers.at("travel_min")[1].get<float>() /
					                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
					auto *slider =
					    dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("speed/" + key)));
					slider->SetProperty("left",
					                    std::to_string(track.x - widget_offset(key).x - track.width * .1f) + "px");
					slider->SetProperty(
					    "top",
					    std::to_string(track.y - widget_offset(key).y + track.height * top - track.width * .2f) + "px");
					slider->SetProperty("width", std::to_string(track.width * 1.2f) + "px");
					slider->SetProperty("height",
					                    std::to_string(track.height * (bottom - top) + track.width * .4f) + "px");
					const double percent =
					    model_.maximum_speed() ? 100.0 * model_.requested_speed() / model_.maximum_speed() : 0;
					slider->SetValue(std::to_string(-percent));
					Rml::ElementList bars;
					slider->GetElementsByTagName(bars, "sliderbar");
					for (auto *bar : bars)
						bar->SetProperty("height", std::to_string(track.width * .4f) + "px");
					if (!pregame)
						app.screen_inputs.push_back(
						    {id("speed/" + key), "Desired speed (%)", std::to_string(percent), true});
				}
				if (kind == "heading" && !pregame)
				{
					auto *field =
					    dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("heading/" + key)));
					if (context_->GetFocusElement() != field)
						field->SetValue(std::string(TextFormat("%.1f", model_.requested_heading() / 1000.0)));
					app.screen_inputs.push_back({id("heading/" + key), "Desired heading (degrees)",
					                             std::string(TextFormat("%.1f", model_.requested_heading() / 1000.0)),
					                             false});
				}
				std::string value =
				    kind == "throttle"
				        ? "Requested " + std::string(TextFormat("%.1f", model_.requested_speed() / 1000.0)) + " m/s"
				    : kind == "heading"
				        ? "Actual " +
				              std::to_string(model_.observation() ? model_.observation()->own.heading / 1000 : 0) +
				              " degrees"
				        : "";
				if (auto *readout = document_->GetElementById("value/" + key))
					readout->SetInnerRML(escaped(localise(value)));
			}
		context_->Update();
		for (const auto &[name, label] : controls_)
		{
			app.screen_controls.emplace_back(name, label);
			place(app, region, name);
		}
		for (const auto &field : app.screen_inputs)
			if (field.id.starts_with(id("")))
				place(app, region, field.id);
		if (pregame)
			place(app, region, id("lobby-title"));
	}
	void draw(const ConsoleRegion &region)
	{
		const Rectangle entire{region.x, region.y, region.width, region.height};
		BeginScissorMode(static_cast<int>(region.x), static_cast<int>(region.y), static_cast<int>(region.width),
		                 static_cast<int>(region.height));
		DrawRectangleRec(entire, {8, 13, 15, 255});
		const auto background = hud("helm-background-9slice");
		const auto &slices = hud_metadata_.at("helm-background-9slice").at("nine_slice");
		DrawTextureNPatch(background,
		                  {{0, 0, static_cast<float>(background.width), static_cast<float>(background.height)},
		                   slices.at("left").get<int>(),
		                   slices.at("top").get<int>(),
		                   slices.at("right").get<int>(),
		                   slices.at("bottom").get<int>(),
		                   NPATCH_NINE_PATCH},
		                  entire, {0, 0}, 0, WHITE);
		const auto &view = model_.observation();
		const float ox = region.x, oy = region.y;
		if (auto *widgets = document_->GetElementById("widgets"))
			for (int i = 0; i < widgets->GetNumChildren(); ++i)
			{
				auto *widget = widgets->GetChild(i);
				const auto key = widget->GetId().substr(7);
				if (auto *dial = document_->GetElementById("dial/" + key))
				{
					const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
					const float radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
					const Vector2 centre{ox + at.x + dial->GetOffsetWidth() / 2,
					                     oy + at.y + dial->GetOffsetHeight() / 2};
					DrawCircleV(centre, radius * .69f, {8, 23, 25, 255});
					for (int ring = 1; ring <= 4; ++ring)
						DrawCircleLinesV(centre, radius * .68f * ring / 4, {37, 77, 78, 255});
					DrawLineV({centre.x - radius * .68f, centre.y}, {centre.x + radius * .68f, centre.y},
					          {37, 77, 78, 255});
					DrawLineV({centre.x, centre.y - radius * .68f}, {centre.x, centre.y + radius * .68f},
					          {37, 77, 78, 255});
					if (view && view->feeds & system_bit(OperatingSystem::Radar))
					{
						const auto &systems = app_->session->catalog().entities.at(view->own.definition).at("systems");
						const double range =
						    systems.contains("radar") ? systems.at("radar").at("range_mm").get<double>() : 1;
						draw_radar_body(radar_textures_, app_->session->catalog(), view->own, centre, 9, WHITE);
						for (const auto &contact : view->contacts)
						{
							const double dx = contact.x - view->own.x, dy = contact.y - view->own.y;
							if (std::hypot(dx, dy) > range)
								continue;
							const Vector2 point{centre.x + static_cast<float>(dx / range) * radius * .66f,
							                    centre.y - static_cast<float>(dy / range) * radius * .66f};
							draw_radar_body(radar_textures_, app_->session->catalog(), contact, point, 7, SKYBLUE);
						}
					}
					image(hud("helm-radar-ring"), {centre.x - radius, centre.y - radius, radius * 2, radius * 2});
					if (heading_dial_)
					{
						const float angle = model_.requested_heading() * 3.141592653589793f / 180000;
						const Vector2 marker{centre.x + std::cos(angle) * radius * .87f,
						                     centre.y - std::sin(angle) * radius * .87f};
						const auto pointer = hud("heading-pointer");
						const float size = std::max(28.f, radius * .2f);
						DrawTexturePro(pointer,
						               {0, 0, static_cast<float>(pointer.width), static_cast<float>(pointer.height)},
						               {marker.x, marker.y, size, size}, {size / 2, size / 2},
						               90 - model_.requested_heading() / 1000.f, WHITE);
					}
				}
				if (document_->GetElementById(id("speed/" + key)))
				{
					auto track = throttle_rect(key);
					track.x += ox;
					track.y += oy;
					image(hud("helm-throttle-track"), track);
					const float fraction = model_.maximum_speed()
					                           ? static_cast<float>(model_.requested_speed()) / model_.maximum_speed()
					                           : 0;
					const auto &marks = hud_metadata_.at("helm-throttle-track").at("markers");
					const float top = marks.at("travel_max")[1].get<float>() /
					                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
					            bottom = marks.at("travel_min")[1].get<float>() /
					                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
					const float y = track.y + track.height * (bottom - fraction * (bottom - top));
					image(hud("helm-throttle-handle"),
					      {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f});
					if (context_->GetFocusElement() == document_->GetElementById(id("speed/" + key)))
						DrawRectangleLinesEx(
						    {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f},
						    2, {255, 190, 94, 255});
				}
				if (auto *readout = document_->GetElementById("value/" + key);
				    readout && readout->GetClassNames() == "readout" && !readout->GetInnerRML().empty())
				{
					const auto at = readout->GetAbsoluteOffset(Rml::BoxArea::Border);
					image(hud("readout-panel"), {ox + at.x - 8, oy + at.y - 8, readout->GetOffsetWidth() + 16,
					                             readout->GetOffsetHeight() + 16});
				}
			}
		EndScissorMode();
	}
};
} // namespace phoenix
