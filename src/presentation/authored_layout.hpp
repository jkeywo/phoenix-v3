// Members of ConsoleSurface. Authored placement uses the same live component elements.
UiThemeRenderer theme_renderer_;
Json active_theme_ = Json::object();
std::optional<UiLayout> authored_program_;
UiResolved authored_resolved_;
std::string authored_geometry_;
struct SurfaceBrowsing
{
	NavigationCamera camera;
	double x = 0, y = 0;
	std::string draft;
	std::map<std::string, float> scroll;
	std::map<std::string, std::string> pages;
};
std::map<std::string, SurfaceBrowsing> surface_browsing_;
std::string browsing_console_, restore_draft_;
bool restore_browsing_ = false, page_focus_pending_ = false;
void sync_console_browsing()
{
	const auto current = model_.observation()
	                         ? std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console
	                         : std::string{};
	if (current == browsing_console_)
		return;
	if (!browsing_console_.empty())
	{
		auto &saved = surface_browsing_[browsing_console_];
		saved.camera = navigation_camera_;
		saved.x = navigation_x_;
		saved.y = navigation_y_;
		saved.pages = authored_pages_;
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
			saved.draft = field->GetValue();
		for (const char *panel :
		     {"comms-history", "comms-list", "gameplay", "authored-body", "gameplay-readouts", "gameplay-messages"})
			if (auto *element = document_->GetElementById(panel))
				saved.scroll[panel] = element->GetScrollTop();
	}
	cancel_pointer();
	model_.release_controls();
	const auto saved = surface_browsing_.contains(current) ? surface_browsing_.at(current) : SurfaceBrowsing{};
	navigation_camera_ = saved.camera;
	navigation_x_ = saved.x;
	navigation_y_ = saved.y;
	authored_pages_ = saved.pages;
	restore_draft_ = saved.draft;
	restore_browsing_ = true;
	browsing_console_ = current;
}
void restore_console_browsing()
{
	if (!restore_browsing_)
		return;
	if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
		field->SetValue(restore_draft_);
	if (surface_browsing_.contains(browsing_console_))
		for (const auto &[panel, scroll] : surface_browsing_.at(browsing_console_).scroll)
			if (auto *element = document_->GetElementById(panel))
				element->SetScrollTop(scroll);
	restore_browsing_ = false;
}
std::map<std::string, std::string> authored_pages_;
std::map<std::string, std::string> authored_parents_;
UiTabs console_tabs_;
std::map<std::string, UiTabs> authored_tab_groups_;
std::string preview_pages_signature_;
std::string authored_identity_, authored_tabs_markup_, console_tabs_markup_, theme_identity_;
std::map<std::string, std::string> authored_page_groups_;

Rml::Element *authored_component(const std::string &target)
{
	static const std::map<std::string, std::string> panels = {{"gameplay", "gameplay"},
	                                                          {"controls", "gameplay-controls"},
	                                                          {"readouts", "gameplay-readouts"},
	                                                          {"messages", "gameplay-messages"},
	                                                          {"engineering", "engineering-cards"},
	                                                          {"sensors", "sensor-panel"},
	                                                          {"conversations", "comms-layer"},
	                                                          {"conversation-list", "comms-list"},
	                                                          {"conversation-history", "comms-history"},
	                                                          {"conversation-replies", "comms-choices"},
	                                                          {"conversation-compose", "comms-compose"},
	                                                          {"conversation-header", "comms-header"},
	                                                          {"conversation-incoming", "comms-incoming"},
	                                                          {"navigation", "navigation-layer"},
	                                                          {"manual-flight", "manual-flight"}};
	if (!panels.contains(target) && model_.layout())
		for (const auto &widget : model_.layout()->at("widgets"))
			if (widget.at("id") == target)
			{
				if (widget.at("kind") == "conversations")
					return document_->GetElementById("comms-layer");
				if (widget.at("kind") == "navigation")
					return document_->GetElementById("navigation-layer");
			}
	return document_->GetElementById(panels.contains(target) ? panels.at(target) : "widget/" + target);
}
bool authored_component_visible(const std::string &target)
{
	auto *element = authored_component(target);
	if (!element)
		return false;
	for (const auto &placement : authored_resolved_.placements)
		if (placement.visible)
		{
			auto *panel = authored_component(placement.component);
			for (auto *ancestor = element; ancestor; ancestor = ancestor->GetParentNode())
				if (ancestor == panel)
					return true;
		}
	return false;
}
void restore_authored_components()
{
	// Restore parents before legacy generators replace their children.
	if (!authored_parents_.empty())
		properties_.clear();
	for (const auto &[target, parent_id] : authored_parents_)
	{
		auto *component = authored_component(target);
		auto *parent = document_->GetElementById(parent_id);
		if (component && parent && component->GetParentNode() != parent)
			parent->AppendChild(component->GetParentNode()->RemoveChild(component));
		if (component)
			for (const char *field : {"left", "top", "width", "height", "display", "position", "overflow",
			                          "pointer-events", "right", "bottom"})
				component->RemoveProperty(field);
	}
	authored_parents_.clear();
	authored_program_.reset();
	authored_resolved_ = UiResolved{};
	authored_identity_.clear();
	authored_geometry_.clear();
}
void apply_console_theme(const Json *layout)
{
	if (!app_->session)
		return;
	const auto &catalog = app_->session->catalog();
	if (catalog.ui_theme.empty() && (!layout || (!layout->contains("layout") && !layout->contains("theme") &&
	                                             !layout->contains("theme_overrides"))))
	{
		theme_renderer_.reset(*document_);
		active_theme_ = Json::object();
		theme_identity_.clear();
		return;
	}
	const auto theme_name = layout ? layout->value("theme", std::string{}) : std::string{};
	const auto overrides = layout ? layout->value("theme_overrides", Json::object()) : Json::object();
	const auto signature = theme_name + overrides.dump() + std::to_string(generation_);
	if (signature == theme_identity_)
		return;
	theme_identity_ = signature;
	try
	{
		active_theme_ = resolved_ui_theme(catalog, theme_name, overrides);
		theme_renderer_.apply(*document_, catalog, theme_name, overrides);
	}
	catch (const std::exception &error)
	{
		app_->accessible_status = error.what();
	}
}
bool authored_action(const std::string &action)
{
	if (action.starts_with("layout-page/"))
	{
		const auto page = action.substr(12);
		if (!authored_page_groups_.contains(page))
			return false;
		cancel_pointer();
		model_.release_controls();
		const auto group = authored_page_groups_.at(page);
		if (!authored_tab_groups_[group].select(page))
			return false;
		authored_pages_[group] = page;
		displayed_values_.clear();
		page_focus_pending_ = true;
		return true;
	}
	if (action.starts_with("game.console/"))
	{
		const auto next = action.substr(13);
		if (!console_tabs_.select(next))
			return false;
		cancel_pointer();
		model_.release_controls();

		return model_.act(action);
	}
	return false;
}
void prepare_console_tabs(const ConsoleRegion &region)
{
	console_tabs_.update(model_.available_tabs());
	if (model_.observation())
		console_tabs_.select(model_.observation()->console);
	std::string markup;
	if (console_tabs_.items().size() > 1)
		for (const auto &tab : console_tabs_.items())
		{
			const auto action = id("game.console/" + tab.id);
			const auto label = localise(tab.label) + (tab.attention ? " — " + localise("Unread") : "");
			markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(action) + "\" class=\"" +
			          (tab.id == console_tabs_.selected() ? "selected" : "") + "\">" +
			          Rml::StringUtilities::EncodeRml(label) + "</button>";
			app_->screen_controls.emplace_back(action, label);
			app_->screen_toggles[action] = tab.id == console_tabs_.selected();
		}
	if (markup != console_tabs_markup_)
	{
		document_->GetElementById("console-tabs")->SetInnerRML(markup);
		console_tabs_markup_ = markup;
	}
	for (const auto &tab : console_tabs_.items())
		place(*app_, region, id("game.console/" + tab.id));
}
void prepare_authored_layout(const Json *layout, const ConsoleRegion &region)
{
	const bool authored = layout && layout->contains("layout") && mission_outcome_.empty();
	document_->SetClass("authored", authored);
	apply_console_theme(layout);
	prepare_console_tabs(region);
	if (!authored)
	{
		if (!mission_outcome_.empty())
		{
			restore_authored_components();
			property(document_->GetElementById("gameplay"), "display", "none");
			property(document_->GetElementById("widgets"), "display", "none");
			property(document_->GetElementById("navigation-layer"), "display", "block");
		}
		return;
	}
	const auto signature = std::to_string(layout_identity_);
	if (authored_identity_ != signature)
	{
		authored_program_ = compile_ui_layout(app_->session->catalog(), *layout);
		authored_identity_ = signature;
	}
	if (context_name_.starts_with("preview-") && layout->contains("_preview_pages"))
	{
		const auto hint = layout->at("_preview_pages").dump();
		if (!layout->value("_preview_interact", false) || hint != preview_pages_signature_)
			authored_pages_ = layout->at("_preview_pages").get<std::map<std::string, std::string>>();
		preview_pages_signature_ = hint;
	}
	std::string geometry = signature + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) + "/" +
	                       std::to_string(text_scale_) + "/" + std::to_string(console_tabs_.items().size());
	for (const auto &[key, value] : authored_pages_)
		geometry += "/" + key + "/" + value;
	if (geometry != authored_geometry_)
	{
		if (!authored_geometry_.empty())
			cancel_pointer();
		authored_resolved_ = resolve_console_ui(*authored_program_, region.width, region.height, text_scale_ / 100.f,
		                                        authored_pages_, console_tabs_.items().size() > 1);
		authored_geometry_ = geometry;
	}
	const auto &resolved = authored_resolved_;
	std::map<std::string, std::vector<UiTab>> groups;
	for (const auto &tab : resolved.pages)
		groups[resolved.page_groups.at(tab.id)].push_back(tab);
	for (auto &[group, items] : groups)
	{
		auto &tabs = authored_tab_groups_[group];
		tabs.update(std::move(items));
		if (authored_pages_.contains(group))
			tabs.select(authored_pages_.at(group));
	}

	property(document_->GetElementById("authored-body"), "top", std::to_string(resolved.body.y) + "dp");
	property(document_->GetElementById("layout-tabs"), "top",
	         std::to_string(resolved.body.y - 48 * text_scale_ / 100.f) + "dp");
	property(document_->GetElementById("layout-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	property(document_->GetElementById("console-tabs"), "top", std::to_string(64 * text_scale_ / 100.f) + "dp");
	property(document_->GetElementById("console-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	auto *host = document_->GetElementById("authored-body");
	// Only hide roots not used as targets: avoid toggling a live root twice per frame.
	std::set<Rml::Element *> targets;
	for (const auto &placement : resolved.placements)
		if (auto *component = authored_component(placement.component))
			targets.insert(component);
	for (const char *panel :
	     {"widgets", "gameplay", "comms-layer", "sensor-panel", "navigation-layer", "manual-flight"})
		if (auto *element = document_->GetElementById(panel); element && !targets.contains(element))
			property(element, "display", "none");
	authored_page_groups_ = resolved.page_groups;
	for (const auto &placement : resolved.placements)
	{
		auto *component = authored_component(placement.component);
		if (!component)
			continue;
		if (!authored_parents_.contains(placement.component))
			authored_parents_[placement.component] = component->GetParentNode()->GetId();
		if (component->GetParentNode() != host)
			host->AppendChild(component->GetParentNode()->RemoveChild(component));
		property(component, "display",
		         placement.visible && (placement.component != "manual-flight" || model_.manual_available()) ? "block"
		                                                                                                    : "none");
		property(component, "position", "absolute");
		property(component, "pointer-events", placement.component == "manual-flight" ? "none" : "auto");
		property(component, "overflow", "auto");
		component->RemoveProperty("right");
		component->RemoveProperty("bottom");
		for (const auto &[key, value] : placement.style.items())
			if (key == "background" || key == "text" || key == "font_family")
				property(component,
				         key == "background" ? "background-color"
				         : key == "text"     ? "color"
				                             : "font-family",
				         value.get<std::string>());
		if (placement.style.contains("font_size"))
			property(component, "font-size",
			         std::to_string(placement.style.at("font_size").get<float>() * text_scale_ / 100.f) + "dp");
		const auto r = placement.bounds;
		property(component, "left", std::to_string(r.x) + "dp");
		property(component, "top", std::to_string(r.y) + "dp");
		property(component, "width", std::to_string(r.width) + "dp");
		property(component, "height", std::to_string(r.height) + "dp");
	}
	std::string markup;
	for (const auto &tab : resolved.pages)
	{
		bool unread = false;
		for (const auto &p : resolved.placements)
			if ((p.id == tab.id || p.id.starts_with(tab.id + ".")) &&
			    (p.component == "conversations" || p.component.starts_with("conversation-")))
				for (const auto &thread : model_.conversations().value("threads", Json::array()))
					unread |= thread.value("unread", false);
		const auto label = localise(tab.label) + (unread ? " — " + localise("Unread") : "");
		const auto group = authored_page_groups_.find(tab.id);
		const bool selected = std::any_of(resolved.placements.begin(), resolved.placements.end(),
		                                  [&](const auto &p)
		                                  {
			                                  return p.visible && (p.id == tab.id || p.id.starts_with(tab.id + "."));
		                                  });
		markup += "<button id=\"" + id("layout-page/" + tab.id) + "\" class=\"" + (selected ? "selected" : "") + "\">" +
		          Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(id("layout-page/" + tab.id), label);
		app_->screen_toggles[id("layout-page/" + tab.id)] = selected;
	}
	if (markup != authored_tabs_markup_)
	{
		document_->GetElementById("layout-tabs")->SetInnerRML(markup);
		authored_tabs_markup_ = markup;
	}
	context_->Update();
	if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
		for (const auto &placement : resolved.placements)
		{
			auto *component = authored_component(placement.component);
			bool contains = false;
			for (auto *ancestor = focus; ancestor; ancestor = ancestor->GetParentNode())
				if (ancestor == component)
					contains = true;
			if (contains)
				for (const auto &tab : resolved.pages)
					if (placement.id == tab.id || placement.id.starts_with(tab.id + "."))
						if (auto *button = document_->GetElementById(id("layout-page/" + tab.id)))
							button->Focus();
		}
	if (page_focus_pending_)
	{
		if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
			for (const auto &[group, page] : authored_pages_)
				if (auto *button = document_->GetElementById(id("layout-page/" + page)))
				{
					button->Focus();
					break;
				}
		page_focus_pending_ = false;
	}
	for (const auto &tab : resolved.pages)
		place(*app_, region, id("layout-page/" + tab.id));
}
