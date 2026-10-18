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
	Application *app_ = nullptr;
	std::string signature_;
	std::map<std::string, std::string> displayed_values_;
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0;
	std::vector<std::pair<std::string, std::string>> controls_;
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
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#d9e9ef;}
button {display:inline-block;padding:8px;margin:4px;border:1px #76c9e8;background:#173142;}
button:hover,button:focus {background:#315a70;}
#toolbar {position:absolute;left:10px;top:4px;right:10px;height:100px;overflow:auto;}
#widgets {position:absolute;left:10px;right:10px;top:115px;bottom:10px;}
.widget {position:absolute;box-sizing:border-box;border:1px #315a70;padding:8px;overflow:auto;}
p {display:block;font-size:18px;}
</style></head><body><div id="toolbar"></div><div id="widgets"></div></body></rml>)");
		if (!document_)
			throw std::runtime_error("Cannot create Console document");
		document_->AddEventListener("click", this);
		document_->Show();
	}
	~ConsoleSurface()
	{
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
		if (app_->screen != Screen::console || owner_.lock() != app_->session ||
		    (app_->session && generation_ != app_->session->generation()))
			return false;
		if (action == "home")
		{
			app_->screen = app_->can_host ? Screen::server_lobby : Screen::client_lobby;
			return true;
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
		action(event.GetTargetElement()->GetId());
	}
	void focus(const std::string &name)
	{
		if (auto *element = document_->GetElementById(name))
			element->Focus(true);
	}
	void prepare(Application &app, const ConsoleRegion &region, const std::set<std::string> &pads)
	{
		app_ = &app;
		model_.refresh(app.session, route_);
		if (owner_.lock() != app.session || (app.session && generation_ != app.session->generation()))
		{
			owner_ = app.session;
			generation_ = app.session ? app.session->generation() : 0;
			signature_.clear();
			document_->Focus();
		}
		context_->SetDimensions({static_cast<int>(region.width), static_cast<int>(region.height)});
		auto *layout = model_.layout();
		std::string signature = (layout ? layout->dump() : "vacant") + (app.pseudo_locale ? "pseudo" : "normal");
		if (model_.observation())
			signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		if (app.session)
			for (const auto &seat : app.session->console_seats())
				signature += std::to_string(seat.address.ship) + seat.address.console + (seat.available ? "1" : "0");
		for (const auto &pad : pads)
			signature += "/pad/" + pad;
		if (signature != signature_)
		{
			signature_ = std::move(signature);
			displayed_values_.clear();
			controls_.clear();
			std::string toolbar =
			    toolbar_enabled_ ? button("home", "Lobby") : "<p>Sample Console / isolated preview</p>";
			if (toolbar_enabled_ && model_.observation())
				toolbar += button("release", "Release Console");
			for (const auto &pad : toolbar_enabled_ ? pads : std::set<std::string>{})
				toolbar += button("pad/" + pad, "Assign controller " + pad);
			if (toolbar_enabled_)
				toolbar += "<p id=\"connection-status\" style=\"font-size:14px;\"></p>";
			document_->GetElementById("toolbar")->SetInnerRML(toolbar);
			std::string widgets;
			if (!layout && app.session)
				for (const auto &seat : app.session->console_seats())
					if (seat.available)
						widgets += button("claim/" + std::to_string(seat.address.ship) + "/" + seat.address.console,
						                  "Claim " + seat.spec.station + " / " + seat.address.console);
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
					           "\"><p>" + escaped(translate(widget.at("label").get<std::string>(), app.pseudo_locale)) +
					           "</p>";
					if (kind == "throttle")
						widgets += button("game.slower/" + key, "Reduce speed") +
						           button("game.faster/" + key, "Increase speed");
					if (kind == "heading")
						widgets += button("game.left/" + key, "Turn left") + button("game.right/" + key, "Turn right");
					if (kind == "radar")
						widgets += button("game.contacts/" + key, "Read radar contacts");
					widgets += "<p id=\"value/" + escaped(key) + "\"></p></div>";
				}
			document_->GetElementById("widgets")->SetInnerRML(widgets);
		}
		if (toolbar_enabled_ && app.session)
		{
			const auto status = app.session->connection_status();
			if (displayed_values_["connection"] != status)
			{
				if (auto *element = document_->GetElementById("connection-status"))
					element->SetInnerRML(escaped(status));
				displayed_values_["connection"] = status;
			}
			if (!model_.observation())
				app.accessible_status = status;
		}
		if (layout)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto kind = widget.at("kind").get<std::string>();
				std::string value;
				if (kind == "throttle")
					value = std::to_string(model_.requested_speed() / 1000.0) + " m/s";
				else if (kind == "heading")
					value = std::to_string(model_.requested_heading() / 1000) + " degrees";
				else if (kind == "status")
					value = "Ship " + std::to_string(model_.observation()->own.id) + " / Console " +
					        model_.observation()->console;
				const auto id = "value/" + widget.at("id").get<std::string>();
				if (displayed_values_[id] != value)
				{
					if (auto *element = document_->GetElementById(id))
						element->SetInnerRML(escaped(value));
					displayed_values_[id] = value;
				}
			}
		context_->Update();
		for (auto [name, label] : controls_)
		{
			if (name.find("game.contacts/") != std::string::npos)
				label += ". " + model_.contacts_text();
			if (name.find("game.faster/") != std::string::npos || name.find("game.slower/") != std::string::npos)
				label += ". Requested speed " + std::to_string(model_.requested_speed() / 1000.0) + " m/s";
			if (name.find("game.left/") != std::string::npos || name.find("game.right/") != std::string::npos)
				label += ". Requested heading " + std::to_string(model_.requested_heading() / 1000) + " degrees";
			app.screen_controls.emplace_back(name, label);
			if (auto *element = document_->GetElementById(name))
			{
				auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
				app.control_placements[name] = {region.x + position.x,
				                                region.y + position.y,
				                                element->GetOffsetWidth(),
				                                element->GetOffsetHeight(),
				                                context_->GetFocusElement() == element,
				                                true};
			}
		}
	}
	void draw(const ConsoleRegion &region)
	{
		const auto &view = model_.observation();
		const auto *layout = model_.layout();
		if (!view || !layout)
			return;
		const auto &systems = app_->session->catalog().entities.at(view->own.definition).at("systems");
		if (!systems.contains("radar"))
			return;
		const auto range = systems.at("radar").at("range_mm").get<double>();
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("kind") == "radar")
			{
				auto *element = document_->GetElementById("widget/" + widget.at("id").get<std::string>());
				auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
				Rectangle area{region.x + position.x + 10, region.y + position.y + 110, element->GetOffsetWidth() - 20,
				               element->GetOffsetHeight() - 120};
				if (area.width < 10 || area.height < 10)
					continue;
				BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width),
				                 static_cast<int>(area.height));
				Vector2 centre{area.x + area.width / 2, area.y + area.height / 2};
				const auto radius = std::min(area.width, area.height) / 2;
				for (int ring = 1; ring <= 4; ++ring)
					DrawCircleLinesV(centre, radius * ring / 4, Color{45, 91, 111, 255});
				draw_radar_icon(view->own.radar_icon, centre, 9, static_cast<float>(view->own.heading), WHITE);
				for (const auto &contact : view->contacts)
				{
					Vector2 point{centre.x + static_cast<float>((contact.x - view->own.x) / range) * radius,
					              centre.y - static_cast<float>((contact.y - view->own.y) / range) * radius};
					draw_radar_icon(contact.radar_icon, point, 7, static_cast<float>(contact.heading), SKYBLUE);
					DrawText(TextFormat("#%llu", static_cast<unsigned long long>(contact.id)),
					         static_cast<int>(point.x + 10), static_cast<int>(point.y), 14, WHITE);
				}
				EndScissorMode();
			}
	}
};
} // namespace phoenix
