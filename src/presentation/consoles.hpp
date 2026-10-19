#pragma once
namespace phoenix
{
class ConsoleSurface final : public Rml::EventListener
{
	ConsoleId route_;
	std::string context_name_;
	bool toolbar_enabled_ = true;
	bool profile_open_ = false;
	std::string profile_name_;
	Bytes profile_avatar_;
	Json terminal_;
	std::map<std::string, Bytes> crew_portraits_;
	Rml::Context *context_ = nullptr;
	Rml::ElementDocument *document_ = nullptr;
	ConsoleModel model_;
	TextureCache radar_textures_;
	Application *app_ = nullptr;
	std::string signature_;
	std::map<std::string, std::string> displayed_values_;
	std::vector<std::pair<std::string, std::string>> readout_values_;
	bool update_readout(const std::string &name, const std::string &text, bool force)
	{
		auto *target = element(id(name));
		if (!target || displayed_values_[name] == text)
			return false;
		auto *panel = document_->GetElementById("gameplay");
		const auto panel_at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto at = target->GetAbsoluteOffset(Rml::BoxArea::Border);
		const bool visible =
		    at.y + target->GetOffsetHeight() >= panel_at.y - 100 && at.y <= panel_at.y + panel->GetOffsetHeight() + 100;
		// Keep measured offscreen heights; accessible values are published separately.
		if (!force && !displayed_values_[name].empty() && !visible)
			return false;
		target->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		displayed_values_[name] = text;
		return true;
	}

	std::map<std::string, Rml::Element *> elements_;
	std::map<std::pair<Rml::Element *, std::string>, std::string> properties_;
	void property(Rml::Element *target, const std::string &name, const std::string &value)
	{
		auto &previous = properties_[{target, name}];
		if (previous != value)
		{
			target->SetProperty(name, value);
			previous = value;
		}
	}
	Rml::Element *element(const std::string &name)
	{
		const auto found = elements_.find(name);
		if (found != elements_.end())
			return found->second;
		return elements_.emplace(name, document_->GetElementById(name)).first->second;
	}
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0;
	std::vector<std::pair<std::string, std::string>> controls_;
	std::string gameplay_signature_;
	std::string placement_signature_;
	std::string gameplay_readout_signature_;
	std::set<std::string> held_clicks_;
	std::map<std::string, Texture2D> hud_;
	std::map<std::string, Json> hud_metadata_;
	std::optional<std::array<float, 3>> heading_dial_;
	std::map<std::uint64_t, std::string> heading_contacts_;
	std::map<std::uint64_t, std::string> throttle_contacts_;
	std::map<std::string, Rectangle> throttle_tracks_;
	bool syncing_values_ = false;
	std::map<std::string, double> throttle_sent_at_;
	std::map<std::string, std::array<float, 3>> heading_dials_;
	std::map<std::string, std::string> dial_systems_;
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
		if (auto *element = this->element(name))
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
	std::string id(const std::string &action) const
	{
		return "console/" + std::to_string(route_) + "/" + action;
	}
	std::string button(const std::string &action, const std::string &label)
	{
		controls_.emplace_back(id(action), label);
		return "<button id=\"" + Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
		       Rml::StringUtilities::EncodeRml(translate(label, app_->pseudo_locale)) + "</button>";
	}

public:
	explicit ConsoleSurface(ConsoleId route, const std::string &prefix = "console-", bool toolbar = true)
	    : route_(route), context_name_(prefix + std::to_string(route)), toolbar_enabled_(toolbar)
	{
		terminal_ =
		    parse_json(read_file(asset_path("../menu/menu-scene.json"), 64 * 1024), "menu scene").at("terminal");
		for (int i = 1; i <= 12; ++i)
			crew_portraits_.emplace(
			    sha256(read_file(asset_path("../menu/avatar-" + std::to_string(i) + ".png"), 12288)),
			    read_file(asset_path("../menu/portrait-" + std::to_string(i) + ".png"), 512 * 1024));
		context_ = Rml::CreateContext(context_name_, {1280, 720});
		if (!context_)
			throw std::runtime_error("Cannot create independent Console context");
		document_ = context_->LoadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#e5e4ca;}
h1 {font-size:26px;display:inline-block;margin:0 18px 8px 0;}
h2 {font-size:20px;display:block;margin:4px 0;}
button {display:inline-block;padding:10px;margin:4px;border:1px #9c8b68;background:#222b2b;color:#f0e3c2;}
button:hover,button:focus {background:#455458;border-color:#ec9c52;}
#toolbar {position:absolute;left:20px;top:18px;right:20px;height:85px;overflow:visible;z-index:2;}
#toolbar h1 {margin-left:76px;}
#connection-status {font-size:15px;}
#widgets {position:absolute;left:4%;right:4%;top:125px;bottom:4%;}
#gameplay {position:absolute;left:60%;right:4%;top:125px;bottom:4%;overflow:auto;background:#0b1719ee;border:1px #988b71;padding:8px;display:none;}
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
.ready-card {position:absolute;left:10%;top:390px;width:80%;box-sizing:border-box;padding:28px;background:#0b1719ee;border:2px #d48a48;}
.profile-card {top:18%;}
.ready-card h1 {display:block;font-size:32px;}
.ready-card button {min-width:120px;}
.ready-card .avatar-choice {min-width:0;width:64px;height:64px;padding:6px;}
.ready-card input {display:block;width:90%;height:40px;background:#211f1a;color:#fff0d4;margin:10px 0;}
.profile-tv {position:absolute;right:20px;top:0;width:264px;height:330px;background:transparent;border:0px;padding:0px;}
.profile-tv img,.avatar-choice img {pointer-events:none;}
.settings-cog {position:absolute;left:0;top:0;width:64px;height:64px;margin:0;padding:0px;background:transparent;border:2px transparent;}
.settings-cog img {position:absolute;left:4px;top:4px;width:54px;height:54px;pointer-events:none;}
.helm #toolbar {right:auto;height:64px;}
.helm #toolbar h1,.helm #connection-status,.helm .radar-widget h2,.helm .heading-widget h2,.helm .heading-widget label,.helm .heading-widget p {display:none;}
.helm .dial {left:0;right:0;top:0;bottom:0;}
.helm .radar-widget button,.helm .heading-value {opacity:0;}
.helm .radar-widget button:focus,.helm .heading-value:focus {opacity:1;}
.helm #gameplay {background:transparent;border:0px;padding:0px;}
.profile-tv span {position:absolute;left:20%;top:69%;width:55%;text-align:center;pointer-events:none;}
</style></head><body><div id="toolbar"></div><div id="widgets"></div><div id="gameplay"><div id="gameplay-controls"></div><div id="gameplay-readouts"></div></div><div id="lobby"></div></body></rml>)");
		if (!document_)
			throw std::runtime_error("Cannot create Console document");
		document_->AddEventListener("click", this);
		document_->AddEventListener("change", this);
		document_->AddEventListener("mousedown", this);
		document_->AddEventListener("mouseup", this);
		document_->AddEventListener("mouseout", this);
		document_->AddEventListener("touchstart", this);
		document_->AddEventListener("touchend", this);
		document_->AddEventListener("touchcancel", this);
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
		if (action == "profile" || action == "profile-save" || action.starts_with("avatar/"))
		{
			if (!app_->session->lobby().active || app_->session->lobby().launched)
				return false;
			if (action == "profile")
				profile_open_ = !profile_open_;
			else if (action == "profile-save")
			{
				if (!app_->session->set_local_player_profile(route_, profile_name_, profile_avatar_))
					return false;
				profile_open_ = false;
			}
			else
			{
				int choice = 0;
				const auto value = action.substr(7);
				if (std::from_chars(value.data(), value.data() + value.size(), choice).ec != std::errc{} ||
				    choice < 1 || choice > 12)
					return false;
				profile_avatar_ = read_file(asset_path("../menu/avatar-" + std::to_string(choice) + ".png"), 12288);
			}
			signature_.clear();
			return true;
		}
		if (action == "ready")
		{
			const auto lobby = app_->session->lobby();
			if (!lobby.active || lobby.launched)
				return false;
			for (const auto &seat : lobby.consoles)
				if (seat.local_screen == route_)
				{
					LobbyCommand command{seat.ready ? LobbyAction::unready : LobbyAction::ready};
					command.screen = route_;
					return app_->session->lobby_command(command);
				}
			return false;
		}

		if (action == "settings")
		{
			app_->settings_console = route_;
			return invoke_navigation(*app_, "settings");
		}
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
		if (syncing_values_)
			return;
		const auto name = event.GetTargetElement()->GetId();
		if (event.GetType() == "mousedown" || event.GetType() == "touchstart")
		{
			if (name.starts_with(id("game.control/")) && model_.hold_control(name.substr(id("").size())))
				held_clicks_.insert(name);
			return;
		}
		if (event.GetType() == "mouseup" || event.GetType() == "mouseout" || event.GetType() == "touchend" ||
		    event.GetType() == "touchcancel")
		{
			if (name.starts_with(id("game.control/")))
				model_.release_control(name.substr(id("").size()));
			if (event.GetType() == "mouseout" || event.GetType() == "touchcancel")
				held_clicks_.erase(name);
			return;
		}
		if (event.GetType() == "click" && held_clicks_.erase(name))
			return;
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
		if (app_ && app_->screen == Screen::server_lobby && name == id("ready"))
			invoke_navigation(*app_, name);
		else
			action(name);
	}
	void focus(const std::string &name)
	{
		if (auto *element = this->element(name))
			element->Focus(true);
	}
	bool set_value(const std::string &name, const std::string &text)
	{
		if (app_ && profile_open_ && name == id("profile-name"))
		{
			profile_name_ = text;
			return true;
		}
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
			accepted = model_.set_speed_fraction(value / 100.0, model_.widget_system(action.substr(6)));
		else if (action.starts_with("heading/"))
			accepted = model_.set_heading_degrees(value, model_.widget_system(action.substr(8)));
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
		if (!app_ || !game_active(*app_) || owner_.lock() != app_->session ||
		    generation_ != app_->session->generation())
			return false;
		if (kind == DeviceEventKind::Down)
			for (const auto &[key, track] : throttle_tracks_)
				if (CheckCollisionPointRec({x, y}, {track.x - track.width * .2f, track.y - track.width * .2f,
				                                    track.width * 1.4f, track.height + track.width * .4f}))
				{
					throttle_contacts_[contact] = key;
					break;
				}
		if (const auto found = throttle_contacts_.find(contact); found != throttle_contacts_.end())
		{
			const auto key = found->second;
			if (kind != DeviceEventKind::Cancel && throttle_tracks_.contains(key))
			{
				const auto track = throttle_tracks_.at(key);
				const double fraction = std::clamp((track.y + track.height - y) / track.height, 0.f, 1.f);
				const auto system = model_.widget_system(key);
				const auto requested =
				    std::llround(model_.system_minimum(system) +
				                 (model_.system_maximum(system) - model_.system_minimum(system)) * fraction);
				if (requested != model_.system_request(system) &&
				    (kind == DeviceEventKind::Down || kind == DeviceEventKind::Up ||
				     GetTime() - throttle_sent_at_[key] >= .05))
				{
					if (model_.set_speed_fraction(fraction, system))
						throttle_sent_at_[key] = GetTime();
				}
			}
			if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
				throttle_contacts_.erase(contact);
			return true;
		}
		if (kind == DeviceEventKind::Down)
		{
			for (const auto &[key, dial] : heading_dials_)
			{
				const auto [cx, cy, radius] = dial;
				const auto length = std::hypot(x - cx, cy - y);
				if (length >= radius * .69f && length <= radius)
				{
					heading_contacts_[contact] = key;
					break;
				}
			}
			if (!heading_contacts_.contains(contact))
				return false;
		}
		if (!heading_contacts_.contains(contact))
			return false;
		const auto key = heading_contacts_.at(contact);
		const auto [cx, cy, radius] = heading_dials_.at(key);
		const float dx = x - cx, dy = cy - y, length = std::hypot(dx, dy);
		if (kind != DeviceEventKind::Cancel && length > 1)
		{
			model_.set_heading_degrees(std::fmod(std::atan2(dy, dx) * 180.0 / 3.141592653589793 + 360.0, 360.0),
			                           dial_systems_.at(key));
			app_->accessible_status = model_.announcement();
		}
		if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			heading_contacts_.erase(contact);
		return true;
	}
	void cancel_pointer()
	{
		heading_contacts_.clear();
		throttle_contacts_.clear();
		model_.release_controls();
		held_clicks_.clear();
	}
	void update_view()
	{
		context_->Update();
		bool changed = false;
		for (const auto &[name, text] : readout_values_)
			changed |= update_readout(name, text, false);
		if (changed)
			context_->Update();
	}
	bool probe_readout_scroll()
	{
		// Explicit graphical smoke: deferred text becomes current on scrolling,
		// while its semantic value was already current before it became visible.
		for (const auto &[name, text] : readout_values_)
			if (displayed_values_[name] != text)
			{
				const auto semantic =
				    std::find(app_->screen_readouts.begin(), app_->screen_readouts.end(), std::pair{id(name), text});
				if (semantic == app_->screen_readouts.end())
				{
					std::cerr << "Deferred readout semantic missing: " << name << std::endl;
					return false;
				}
				auto *panel = document_->GetElementById("gameplay");
				const float previous_scroll = panel->GetScrollTop();
				element(id(name))->ScrollIntoView({Rml::ScrollAlignment::Start, Rml::ScrollAlignment::Nearest,
				                                   Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
				update_view();
				if (displayed_values_[name] != text)
				{
					std::cerr << "Deferred readout did not refresh: " << name << std::endl;
					return false;
				}
				panel->SetScrollTop(previous_scroll);
				update_view();
				std::cout << "Deferred readout scroll and current semantics passed" << std::endl;
				return true;
			}
		return false;
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
				const auto track = throttle_tracks_.at(widget.at("id").get<std::string>());
				pointer(998, track.x + track.width / 2, track.y + track.height * .25f, DeviceEventKind::Down);
				pointer(998, track.x + track.width / 2, track.y + track.height * .25f, DeviceEventKind::Up);
				throttle = model_.requested_speed() > 0;
			}
		const auto [x, y, r] = *heading_dial_;
		return throttle && pointer(999, x, y - r * .85f, DeviceEventKind::Down) &&
		       pointer(999, x, y - r * .85f, DeviceEventKind::Up) && model_.requested_heading() == 90000;
	}
	void prepare(Application &app, const ConsoleRegion &region, const std::set<std::string> &pads)
	{
		app_ = &app;
		const auto preparation_started = GetTime();
		const auto previous_announcement = model_.announcement();
		model_.refresh(app.session, route_);
		app.frame_metrics["console/" + std::to_string(route_) + "/projection"] = GetTime() - preparation_started;
		const auto layout_started = GetTime();
		if (model_.announcement() != previous_announcement && !model_.announcement().empty())
			app.accessible_status = model_.announcement();
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
		const bool helm = layout && std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
		                                        [](const auto &widget)
		                                        {
			                                        return widget.at("kind") == "throttle";
		                                        });
		document_->SetClass("helm", helm && !pregame);
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
					for (const auto &member : lobby.members)
						if (member.id == seat.member && !profile_open_)
						{
							profile_name_ = member.name;
							profile_avatar_ = member.avatar;
							if (profile_avatar_.empty())
							{
								profile_avatar_ = read_file(
								    asset_path("../menu/avatar-" + std::to_string(GetRandomValue(1, 12)) + ".png"),
								    12288);
								app.session->set_local_player_profile(route_, profile_name_, profile_avatar_);
							}
						}
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
		                        (pregame ? ready ? "ready" : "waiting" : "running") + (profile_open_ ? "profile" : "") +
		                        (profile_open_ ? "" : profile_name_);
		if (model_.observation())
			signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		for (const auto &pad : pads)
			signature += "/pad/" + pad;
		if (signature != signature_)
		{
			signature_ = std::move(signature);
			placement_signature_.clear();
			properties_.clear();
			elements_.clear();
			gameplay_signature_.clear();
			gameplay_readout_signature_.clear();
			displayed_values_.clear();
			controls_.clear();
			heading_contacts_.clear();
			std::string toolbar = "<h1>" + Rml::StringUtilities::EncodeRml(localise(title)) + "</h1>";
			if (toolbar_enabled_ && !pregame)
			{
				button("settings", "Settings");
				toolbar += "<button class=\"settings-cog\" id=\"" + id("settings") + "\"><img src=\"" +
				           asset_path("../menu/settings.png") + "\" width=\"56\" height=\"56\"/></button>";
			}
			for (const auto &pad : toolbar_enabled_ && !pregame ? pads : std::set<std::string>{})
				toolbar += button("pad/" + pad, "Assign controller " + pad);
			toolbar += "<p id=\"connection-status\"></p>";
			document_->GetElementById("toolbar")->SetInnerRML(toolbar);
			std::string widgets;
			bool has_radar = false;
			int heading_count = 0;
			if (layout && !pregame)
				for (const auto &widget : layout->at("widgets"))
				{
					if (widget.at("kind") == "radar")
						has_radar = true;
					if (widget.at("kind") == "heading")
						++heading_count;
				}
			if (layout && !pregame)
				for (const auto &widget : layout->at("widgets"))
				{
					const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
					auto percent = [&](const char *field)
					{
						return std::to_string(widget.at(field).get<double>() / 10) + "%";
					};
					widgets += "<div class=\"widget " + kind + "-widget\" id=\"widget/" +
					           Rml::StringUtilities::EncodeRml(key) + "\" style=\"left:" + percent("x") +
					           ";top:" + percent("y") + ";width:" + percent("width") + ";height:" + percent("height") +
					           "\"><h2>" + Rml::StringUtilities::EncodeRml(localise(widget.at("label"))) + "</h2>";
					if (kind == "throttle")
						widgets += "<input class=\"throttle\" id=\"" + id("speed/" + key) +
						           "\" type=\"range\" orientation=\"vertical\" min=\"-100\" max=\"0\" step=\"0.1\" "
						           "value=\"0\"" +
						           (pregame ? " disabled" : "") + "/>";
					if (kind == "heading")
					{
						widgets += "<label for=\"" + id("heading/" + key) + "\">" +
						           Rml::StringUtilities::EncodeRml(localise("Desired heading (degrees)")) +
						           "</label><input class=\"heading-value\" id=\"" + id("heading/" + key) +
						           "\" type=\"text\" maxlength=\"7\" value=\"0\"" + (pregame ? " disabled" : "") +
						           "/><p>" +
						           Rml::StringUtilities::EncodeRml(
						               localise("Click or drag the radar ring. East 0; North 90.")) +
						           "</p>";
						if (!has_radar || heading_count > 1)
							widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>";
					}
					if (kind == "radar")
						widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>" +
						           button("game.contacts/" + key, "Read radar contacts");
					widgets += "<p class=\"" + std::string(kind == "throttle" ? "readout" : "heading-actual") +
					           "\" id=\"value/" + Rml::StringUtilities::EncodeRml(key) + "\"/></div>";
				}
			document_->GetElementById("widgets")->SetInnerRML(pregame ? "" : widgets);
			document_->GetElementById("toolbar")->SetProperty("display", pregame ? "none" : "block");
			std::string lobby;
			if (pregame)
				lobby = "<div class=\"ready-card\"><h1 id=\"" + id("lobby-title") + "\">" +
				        Rml::StringUtilities::EncodeRml(localise(title)) + "</h1><p>" +
				        Rml::StringUtilities::EncodeRml(localise(description)) + "</p><p>" +
				        Rml::StringUtilities::EncodeRml(
				            localise(ready ? "Ready. Waiting for the crew." : "Check your Console, then ready up.")) +
				        "</p>" + button("ready", ready ? "Not Ready" : "Ready") + "<p id=\"countdown\"/></div>";
			if (pregame)
			{
				button("profile", "Edit profile");
				const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0),
				            sh = terminal_.at("size").at(1);
				const auto &aperture = terminal_.at("screen");
				const float ax = aperture.at("x"), ay = aperture.at("y"), aw = aperture.at("width"),
				            ah = aperture.at("height");
				lobby += "<button id=\"" + id("profile") +
				         "\" class=\"profile-tv\" style=\"width:" + std::to_string(tw) +
				         "px;height:" + std::to_string(tw * sh / sw) + "px\"><img src=\"" +
				         asset_path("../menu/crt-terminal.png") + "\" width=\"" + std::to_string(tw) +
				         "\"/><span style=\"left:" + std::to_string(ax / sw * 100) +
				         "%;top:" + std::to_string((ay + ah * .85f) / sh * 100) +
				         "%;width:" + std::to_string(aw / sw * 100) +
				         "%;height:" + std::to_string(ah * .15f / sh * 100) + "%;background:#101918;font-size:16px\">" +
				         Rml::StringUtilities::EncodeRml(profile_name_) + "</span></button>";
				if (profile_open_)
				{
					controls_.clear();
					lobby = "<div class=\"ready-card profile-card\"><h1>" +
					        Rml::StringUtilities::EncodeRml(localise("Profile")) + "</h1><label>" +
					        Rml::StringUtilities::EncodeRml(localise("Player name")) + "</label><input id=\"" +
					        id("profile-name") + "\" type=\"text\" maxlength=\"64\" value=\"" +
					        Rml::StringUtilities::EncodeRml(profile_name_) + "\"/><div>";
					for (int i = 1; i <= 12; ++i)
					{
						button("avatar/" + std::to_string(i), "Avatar " + std::to_string(i));
						lobby += "<button class=\"avatar-choice\" id=\"" + id("avatar/" + std::to_string(i)) +
						         "\"><img src=\"" + asset_path("../menu/avatar-" + std::to_string(i) + ".png") +
						         "\" width=\"48\" height=\"48\"/></button>";
					}
					lobby += "</div>" + button("profile-save", "Use this profile") + "</div>";
				}
			}
			document_->GetElementById("lobby")->SetInnerRML(lobby);
		}
		if (pregame && profile_open_)
			app.screen_inputs.push_back({id("profile-name"), localise("Player name"), profile_name_});
		if (pregame)
			app.screen_readouts.emplace_back(id("lobby-title"), localise(title) + ". " + localise(description));
		if (auto *status = document_->GetElementById("connection-status"))
		{
			const auto text = localise(pregame ? description : model_.announcement());
			if (displayed_values_["connection-status"] != text)
			{
				status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
				displayed_values_["connection-status"] = text;
			}
		}
		if (pregame)
			if (auto *countdown = document_->GetElementById("countdown"))
			{
				const auto left = app.session->lobby().countdown_ms;
				countdown->SetInnerRML(left < 0 ? ""
				                                : Rml::StringUtilities::EncodeRml(localise("Starting in")) + " " +
				                                      std::to_string((left + 999) / 1000));
			}
		const auto readouts_started = GetTime();
		auto gameplay_controls =
		    pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_controls();
		std::erase_if(gameplay_controls,
		              [](const auto &control)
		              {
			              // ConsoleModel encodes descriptor IDs as hexadecimal.
			              return control.first == "game.control/61776179";
		              });
		readout_values_ = pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_readouts();
		const auto &gameplay_readouts = readout_values_;
		app.frame_metrics["console/" + std::to_string(route_) + "/readouts"] = GetTime() - readouts_started;
		auto *gameplay = document_->GetElementById("gameplay");
		auto *widgets = document_->GetElementById("widgets");
		const bool has_gameplay = !gameplay_controls.empty() || !gameplay_readouts.empty();
		const auto placement = signature_ + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) +
		                       (has_gameplay ? "/gameplay" : "/empty");
		const bool placement_changed = placement_signature_ != placement;
		if (placement_changed)
		{
			placement_signature_ = placement;
			gameplay->SetProperty("display", has_gameplay ? "block" : "none");
			widgets->SetProperty("right", "4%");
			widgets->SetProperty("bottom", "4%");
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("right", "4%");
			gameplay->SetProperty("top", "125px");
			gameplay->SetProperty("bottom", "4%");
			gameplay->RemoveProperty("width");
			gameplay->RemoveProperty("height");
			bool placed_gameplay = false;
			if (has_gameplay && layout && layout->contains("gameplay_panel") &&
			    layout->at("gameplay_panel").is_object())
			{
				const auto &panel = layout->at("gameplay_panel");
				if (panel.contains("x") && panel.contains("y") && panel.contains("width") && panel.contains("height") &&
				    panel.at("x").is_number() && panel.at("y").is_number() && panel.at("width").is_number() &&
				    panel.at("height").is_number())
				{
					const double x = panel.at("x"), y = panel.at("y"), width = panel.at("width"),
					             height = panel.at("height");
					if (std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
					    x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= 1000 && y + height <= 1000)
					{
						const auto available_height = std::max(80.f, region.height - 125.f);
						gameplay->SetProperty("left", std::to_string(region.width * x / 1000) + "px");
						gameplay->SetProperty("top", std::to_string(125 + available_height * y / 1000) + "px");
						gameplay->SetProperty("width", std::to_string(region.width * width / 1000) + "px");
						gameplay->SetProperty("height", std::to_string(available_height * height / 1000) + "px");
						gameplay->RemoveProperty("right");
						gameplay->RemoveProperty("bottom");
						placed_gameplay = true;
					}
				}
			}
			if (has_gameplay && !placed_gameplay && layout && !layout->at("widgets").empty())
			{
				if (region.width >= 900)
				{
					widgets->SetProperty("right", "42%");
					gameplay->SetProperty("left", "60%");
				}
				else
				{
					widgets->SetProperty("bottom", "45%");
					gameplay->SetProperty("top", "58%");
				}
			}
			if (helm && !pregame)
			{
				widgets->SetProperty("top", "4%");
				widgets->SetProperty("right", "4%");
				widgets->SetProperty("bottom", "4%");
				const float width = region.width * .92f, height = region.height * .92f;
				const float side = std::min(height, width * .70f);
				for (const auto &widget : layout->at("widgets"))
				{
					auto *element = document_->GetElementById("widget/" + widget.at("id").get<std::string>());
					const std::string kind = widget.at("kind");
					if (kind == "radar")
					{
						element->SetProperty("left", std::to_string((width - side) / 2) + "px");
						element->SetProperty("top", std::to_string((height - side) / 2) + "px");
						element->SetProperty("width", std::to_string(side) + "px");
						element->SetProperty("height", std::to_string(side) + "px");
					}
					else if (kind == "throttle")
					{
						element->SetProperty("left", "85%");
						element->SetProperty("top", "10%");
						element->SetProperty("width", "15%");
						element->SetProperty("height", "80%");
					}
					else if (kind == "heading")
					{
						element->SetProperty("left", "0%");
						element->SetProperty("top", "85%");
						element->SetProperty("width", "15%");
						element->SetProperty("height", "15%");
					}
				}
				gameplay->SetProperty("left", "4%");
				gameplay->SetProperty("top", "18%");
				gameplay->SetProperty("width", "12%");
				gameplay->SetProperty("height", "60%");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
		}
		Json gameplay_identity = Json::array();
		for (const auto &[name, label] : gameplay_controls)
			gameplay_identity.push_back(name);
		const auto gameplay_signature = gameplay_identity.dump();
		if (gameplay_signature_ != gameplay_signature)
		{
			elements_.clear();
			model_.release_controls();
			held_clicks_.clear();
			std::string markup;
			for (const auto &[name, label] : gameplay_controls)
				markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
				          Rml::StringUtilities::EncodeRml(label) + "</button>";
			document_->GetElementById("gameplay-controls")->SetInnerRML(markup);
			gameplay_signature_ = gameplay_signature;
			for (const auto &[name, label] : gameplay_controls)
				displayed_values_.erase("control/" + name);
		}
		for (const auto &[name, label] : gameplay_controls)
			if (displayed_values_["control/" + name] != label)
			{
				if (auto *element = this->element(id(name)))
					element->SetInnerRML(Rml::StringUtilities::EncodeRml(label));
				displayed_values_["control/" + name] = label;
			}
		gameplay_identity = Json::array();
		for (const auto &[name, text] : gameplay_readouts)
			gameplay_identity.push_back(name);
		const auto readout_signature = gameplay_identity.dump();
		if (gameplay_readout_signature_ != readout_signature)
		{
			elements_.clear();
			std::string markup;
			for (const auto &[name, text] : gameplay_readouts)
			{
				markup += "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"></p>";
				displayed_values_.erase(name);
			}
			document_->GetElementById("gameplay-readouts")->SetInnerRML(markup);
			gameplay_readout_signature_ = readout_signature;
		}
		for (const auto &[name, text] : gameplay_readouts)
		{
			update_readout(name, text, placement_changed);
			app.screen_readouts.emplace_back(id(name), text);
		}
		{
			const auto update_started = GetTime();
			context_->Update();
			app.frame_metrics["console/" + std::to_string(route_) + "/before-widgets"] = GetTime() - update_started;
		}
		heading_dial_.reset();
		heading_dials_.clear();
		dial_systems_.clear();
		throttle_tracks_.clear();
		std::string shared_heading;
		int heading_count = 0;
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("kind") == "heading")
				{
					shared_heading = model_.widget_system(widget.at("id").get<std::string>());
					++heading_count;
				}
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
				if (kind == "radar" || kind == "heading")
					if (auto *dial = document_->GetElementById("dial/" + key))
					{
						const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
						const float radius =
						    std::max(1.f, std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f);
						const auto system = kind == "heading"    ? model_.widget_system(key)
						                    : heading_count == 1 ? shared_heading
						                                         : std::string{};
						if (!system.empty())
						{
							heading_dials_[key] = {at.x + dial->GetOffsetWidth() / 2,
							                       at.y + dial->GetOffsetHeight() / 2, radius};
							dial_systems_[key] = system;
							if (!heading_dial_)
								heading_dial_ = heading_dials_.at(key);
						}
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
					property(slider, "left", std::to_string(track.x - widget_offset(key).x - track.width * .1f) + "px");
					property(slider, "top",
					         std::to_string(track.y - widget_offset(key).y + track.height * top - track.width * .2f) +
					             "px");
					property(slider, "width", std::to_string(track.width * 1.2f) + "px");
					property(slider, "height",
					         std::to_string(track.height * (bottom - top) + track.width * .4f) + "px");
					const auto system = model_.widget_system(key);
					const auto minimum = model_.system_minimum(system), maximum = model_.system_maximum(system);
					const double percent =
					    maximum > minimum ? 100.0 * (model_.system_request(system) - minimum) / (maximum - minimum) : 0;
					throttle_tracks_[key] = {track.x, track.y + track.height * top, track.width,
					                         track.height * (bottom - top)};
					// Programmatic projection updates must never enqueue a fresh player command.
					syncing_values_ = true;
					if (slider->GetValue() != std::to_string(-percent))
						slider->SetValue(std::to_string(-percent));
					syncing_values_ = false;
					Rml::ElementList bars;
					slider->GetElementsByTagName(bars, "sliderbar");
					for (auto *bar : bars)
						property(bar, "height", std::to_string(track.width * .4f) + "px");
					if (!pregame)
						app.screen_inputs.push_back(
						    {id("speed/" + key), "Desired speed (%)", std::to_string(percent), true});
				}
				if (kind == "heading" && !pregame)
				{
					auto *field =
					    dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("heading/" + key)));
					const auto requested = std::string(
					    TextFormat("%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0));
					if (context_->GetFocusElement() != field && field->GetValue() != requested)
						field->SetValue(requested);
					app.screen_inputs.push_back(
					    {id("heading/" + key), "Desired heading (degrees)",
					     std::string(TextFormat(
					         "%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0)),
					     false});
				}
				std::string value =
				    kind == "throttle" ? "Requested " +
				                             std::string(TextFormat(
				                                 "%.1f", model_.system_request(model_.widget_system(key)) / 1000.0)) +
				                             " m/s"
				    : kind == "heading"
				        ? "Actual " +
				              std::to_string(model_.observation() ? model_.observation()->own.heading / 1000 : 0) +
				              " degrees"
				        : "";
				value = localise(value);
				if (auto *readout = document_->GetElementById("value/" + key);
				    readout && displayed_values_["value/" + key] != value)
				{
					readout->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
					displayed_values_["value/" + key] = value;
				}
			}
		{
			const auto update_started = GetTime();
			context_->Update();
			app.frame_metrics["console/" + std::to_string(route_) + "/after-widgets"] = GetTime() - update_started;
		}
		for (const auto &[name, label] : controls_)
		{
			app.screen_controls.emplace_back(name, label);
			place(app, region, name);
		}
		for (const auto &[name, label] : gameplay_controls)
		{
			app.screen_controls.emplace_back(id(name), label);
			place(app, region, id(name));
		}
		for (const auto &[name, text] : gameplay_readouts)
			place(app, region, id(name));
		for (const auto &field : app.screen_inputs)
			if (field.id.starts_with(id("")))
				place(app, region, field.id);
		if (pregame)
			place(app, region, id("lobby-title"));
		app.frame_metrics["console/" + std::to_string(route_) + "/layout"] = GetTime() - layout_started;
	}
	void draw_profile(const ConsoleRegion &region)
	{
		if (app_->session->lobby().active && !app_->session->lobby().launched && !profile_open_ &&
		    !profile_avatar_.empty())
		{
			const auto key = sha256(profile_avatar_);
			const auto &portrait = crew_portraits_.contains(key) ? crew_portraits_.at(key) : profile_avatar_;
			const auto texture = radar_textures_.load(key + "/portrait", portrait);
			const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0), scale = tw / sw;
			const auto &screen = terminal_.at("screen");
			const float x = screen.at("x"), y = screen.at("y"), w = screen.at("width"), h = screen.at("height");
			const Rectangle area{region.x + region.width - 20 - tw + x * scale, region.y + y * scale, w * scale,
			                     h * .85f * scale};
			const float fill = std::max(area.width / texture.width, area.height / texture.height);
			DrawTexturePro(texture,
			               {(texture.width - area.width / fill) / 2, (texture.height - area.height / fill) / 2,
			                area.width / fill, area.height / fill},
			               area, {0, 0}, 0, WHITE);
		}
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
						auto radar = model_.widget_system(key);
						const auto selected = std::find_if(view->systems.begin(), view->systems.end(),
						                                   [&](const auto &s)
						                                   {
							                                   return s.id == radar && s.kind == OperatingSystem::Radar;
						                                   });
						if (selected == view->systems.end())
							radar = select_system(view->systems, OperatingSystem::Radar, {}, false).value_or("");
						const double range = std::max<std::int64_t>(1, model_.system_maximum(radar));
						draw_radar_body(radar_textures_, app_->session->catalog(), view->own, centre, 9, WHITE);
						for (const auto &contact : view->contacts)
						{
							if (!std::any_of(view->systems.begin(), view->systems.end(),
							                 [&](const auto &s)
							                 {
								                 return s.id == radar && s.enabled;
							                 }))
								continue;
							const double dx = contact.x - view->own.x, dy = contact.y - view->own.y;
							if (std::hypot(dx, dy) > range)
								continue;
							const Vector2 point{centre.x + static_cast<float>(dx / range) * radius * .66f,
							                    centre.y - static_cast<float>(dy / range) * radius * .66f};
							draw_radar_body(radar_textures_, app_->session->catalog(), contact, point, 7, SKYBLUE);
						}
					}
					image(hud("helm-radar-ring"), {centre.x - radius, centre.y - radius, radius * 2, radius * 2});
					if (dial_systems_.contains(key))
					{
						const auto heading = model_.system_request(dial_systems_.at(key), "heading_mdeg");
						const float angle = heading * 3.141592653589793f / 180000;
						const Vector2 marker{centre.x + std::cos(angle) * radius * .87f,
						                     centre.y - std::sin(angle) * radius * .87f};
						const auto pointer = hud("heading-pointer");
						const float size = std::max(28.f, radius * .2f);
						DrawTexturePro(
						    pointer, {0, 0, static_cast<float>(pointer.width), static_cast<float>(pointer.height)},
						    {marker.x, marker.y, size, size}, {size / 2, size / 2}, 90 - heading / 1000.f, WHITE);
					}
				}
				if (document_->GetElementById(id("speed/" + key)))
				{
					auto track = throttle_rect(key);
					track.x += ox;
					track.y += oy;
					image(hud("helm-throttle-track"), track);
					const auto system = model_.widget_system(key);
					const auto minimum = model_.system_minimum(system), maximum = model_.system_maximum(system);
					const float fraction =
					    maximum > minimum ? std::clamp(static_cast<float>(model_.system_request(system) - minimum) /
					                                       (maximum - minimum),
					                                   0.f, 1.f)
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
