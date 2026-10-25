// Members of ConsoleSurface. Authored placement uses the same live component elements.
UiThemeRenderer theme_renderer_;
Json active_theme_ = Json::object();
std::optional<UiLayout> authored_program_;
UiResolved authored_resolved_;
std::string authored_geometry_, authored_dom_;
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
std::string preview_pages_signature_;
bool preview_interact_ = false;
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
void return_authored_components()
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
			for (const char *field :
			     {"left", "top", "width", "height", "display", "position", "overflow", "pointer-events", "right",
			      "bottom", "min-width", "min-height", "max-width", "max-height", "box-sizing", "flex"})
				component->RemoveProperty(field);
	}
	authored_parents_.clear();
}
void restore_authored_components()
{
	return_authored_components();
	document_->GetElementById("authored-body")->SetInnerRML("");
	authored_dom_.clear();
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
	if (action.starts_with("station-page/"))
	{
		const auto page = action.substr(13);
		if (page != "logistics" && page != "docking" && page != "away" && page != "craft" && page != "personnel")
			return false;
		cancel_pointer();
		settings_open_ = true;
		local_settings_tab_ = page;
		document_->SetClass("local-settings-open", true);
		return true;
	}
	if (settings_open_)
	{
		settings_open_ = false;
		document_->SetClass("local-settings-open", false);
		document_->SetClass("station-page-open", false);
	}
	if (action.starts_with("layout-page/"))
	{
		const auto page = action.substr(12);
		if (!authored_page_groups_.contains(page))
			return false;
		cancel_pointer();
		model_.release_controls();
		const auto group = authored_page_groups_.at(page);
		authored_pages_[group] = page;
		displayed_values_.clear();
		page_focus_pending_ = true;
		return true;
	}
	if (action.starts_with("game.console/"))
	{
		cancel_pointer();
		model_.release_controls();

		return model_.act(action);
	}
	return false;
}
void prepare_console_tabs(const ConsoleRegion &region)
{
	std::string markup;
	if (!model_.available_tabs().empty())
		for (const auto &tab : model_.available_tabs())
		{
			const auto action = id("game.console/" + tab.id);
			const auto label = localise(tab.label) + (tab.attention ? " — " + localise("Unread") : "");
			markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(action) + "\" class=\"" +
			          (!settings_open_ && tab.id == model_.observation()->console ? "selected" : "") + "\">" +
			          Rml::StringUtilities::EncodeRml(label) + "</button>";
			app_->screen_controls.emplace_back(action, label);
			app_->screen_toggles[action] = !settings_open_ && tab.id == model_.observation()->console;
		}
	if (!settings_open_)
		markup += authored_tabs_markup_;
	for (const auto &[page, label] : std::vector<std::pair<std::string, std::string>>{{"logistics", "Operations"},
	                                                                                  {"docking", "Docking"},
	                                                                                  {"craft", "Flight operations"},
	                                                                                  {"personnel", "Personnel"},
	                                                                                  {"away", "Away missions"}})
	{
		const auto action = id("station-page/" + page);
		markup += "<button id=\"" + action + "\" class=\"" +
		          (settings_open_ && local_settings_tab_ == page ? "selected" : "") + "\">" +
		          Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
		app_->screen_controls.emplace_back(action, localise(label));
		app_->screen_toggles[action] = settings_open_ && local_settings_tab_ == page;
	}
	if (markup != console_tabs_markup_)
	{
		elements_.clear();
		document_->GetElementById("console-tabs")->SetInnerRML(markup);
		console_tabs_markup_ = markup;
	}
	for (const auto &tab : model_.available_tabs())
		place(*app_, region, id("game.console/" + tab.id));
	for (const auto *page : {"logistics", "docking", "craft", "personnel", "away"})
		place(*app_, region, id(std::string("station-page/") + page));
}
std::string layout_node_id(const UiNode &node) const
{
	return "layout-node/" + node.id;
}
void build_authored_tree(const UiNode &root)
{
	return_authored_components();
	std::function<std::string(const UiNode &)> markup = [&](const UiNode &node)
	{
		std::string result =
		    "<div id=\"" + Rml::StringUtilities::EncodeRml(layout_node_id(node)) + "\" class=\"layout-node\">";
		for (const auto &child : node.children)
			result += markup(child);
		return result + "</div>";
	};
	document_->GetElementById("authored-body")->SetInnerRML(markup(root));
	for (const auto &placement : authored_resolved_.placements)
		if (auto *component = authored_component(placement.component))
		{
			authored_parents_[placement.component] = component->GetParentNode()->GetId();
			auto *slot = document_->GetElementById("layout-node/" + placement.id);
			slot->AppendChild(component->GetParentNode()->RemoveChild(component));
			for (const char *field : {"right", "bottom"})
				component->RemoveProperty(field);
		}
}
void style_authored_tree(const UiNode &node, const UiNode *parent = nullptr, bool visible = true)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	const auto unit = [&](float value)
	{
		return std::to_string(value * text_scale_ / 100.f) + "dp";
	};
	if (node.component == "manual-flight" && !model_.manual_available())
		visible = false;
	property(box, "display", visible ? "flex" : "none");
	property(box, "position",
	         !parent || parent->kind == "instruments" || parent->kind == "cutout" ? "absolute" : "relative");
	property(box, "min-width", unit(node.minimum_width));
	property(box, "min-height", unit(node.minimum_height));
	property(box, "flex-direction", node.kind == "row" || node.kind == "wrap" ? "row" : "column");
	property(box, "flex-wrap", node.kind == "wrap" ? "wrap" : "nowrap");
	property(box, "gap", unit(node.gap));
	property(box, "padding",
	         unit(node.children.empty() || node.kind == "instruments" || node.kind == "cutout" ? 0 : node.padding));
	property(box, "align-self", node.align == "start" ? "flex-start" : node.align == "end" ? "flex-end" : node.align);
	property(box, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
	if (!parent)
	{
		property(box, "left", "0dp");
		property(box, "top", "0dp");
		property(box, "width", "100%");
		property(box, "height", "100%");
	}
	else if (parent->kind != "instruments" && parent->kind != "cutout")
	{
		const bool row = parent->kind == "row" || parent->kind == "wrap";
		const auto basis = row ? std::max(node.minimum_width, node.preferred_width)
		                       : std::max(node.minimum_height, node.preferred_height);
		property(box, "flex", std::to_string(parent->kind == "wrap" ? 0.f : node.stretch) + " 0 " + unit(basis));
		property(box, "width",
		         parent->kind == "wrap" ? unit(std::max(node.minimum_width, node.preferred_width)) : "auto");
		property(box, "height",
		         parent->kind == "wrap" ? unit(std::max(node.minimum_height, node.preferred_height)) : "auto");
	}
	if (!node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			property(component, "display", visible ? "block" : "none");
			property(component, "flex", "1 1 0dp");
			if (node.aspect <= 0)
			{
				property(component, "position", "relative");
				property(component, "left", "0dp");
				property(component, "top", "0dp");
				property(component, "width", "100%");
				property(component, "height", "100%");
			}
			property(component, "box-sizing", "border-box");
			property(component, "max-width", "none");
			property(component, "max-height", "none");
			property(component, "min-width", "0dp");
			property(component, "min-height", "0dp");
			property(component, "overflow", "auto");
			property(component, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
			for (const auto &[key, value] : node.style.items())
				if (key == "background" || key == "text" || key == "font_family")
					property(component,
					         key == "background" ? "background-color"
					         : key == "text"     ? "color"
					                             : "font-family",
					         value.get<std::string>());
			if (node.style.contains("font_size"))
				property(component, "font-size", unit(node.style.at("font_size").get<float>()));
		}
	for (const auto &child : node.children)
		style_authored_tree(child, &node, visible && (node.kind != "tabs" || authored_pages_.at(node.id) == child.id));
}
void place_authored_instruments(const UiNode &node)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	if (node.kind == "instruments" || node.kind == "cutout")
	{
		const float density = context_->GetDensityIndependentPixelRatio();
		const float pad = node.padding * text_scale_ / 100.f;
		const float width = std::max(1.f, box->GetClientWidth() / density - pad * 2),
		            height = std::max(1.f, box->GetClientHeight() / density - pad * 2);
		for (const auto &child : node.children)
		{
			UiRect bounds{width * child.rect.x / 1000, height * child.rect.y / 1000, width * child.rect.width / 1000,
			              height * child.rect.height / 1000};
			if (node.kind == "cutout" && !child.region.empty())
			{
				const float x = width * node.circle_x, y = height * node.circle_y;
				const float radius = std::min(width, height) * node.circle_radius, corner = radius / std::sqrt(2.f);
				const bool right = child.region.ends_with("right"), bottom = child.region.starts_with("bottom");
				bounds = child.region == "centre" ? UiRect{x - radius, y - radius, radius * 2, radius * 2}
				                                  : UiRect{right ? x + corner : 0, bottom ? y + corner : 0,
				                                           right ? width - x - corner : x - corner,
				                                           bottom ? height - y - corner : y - corner};
			}
			auto *slot = document_->GetElementById(layout_node_id(child));
			for (const auto &[field, value] : std::map<std::string, float>{{"left", pad + bounds.x},
			                                                               {"top", pad + bounds.y},
			                                                               {"width", bounds.width},
			                                                               {"height", bounds.height}})
				property(slot, field, std::to_string(value) + "dp");
		}
	}
	for (const auto &child : node.children)
		place_authored_instruments(child);
}
void fit_authored_aspects(const UiNode &node)
{
	if (node.aspect > 0 && !node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			auto *box = document_->GetElementById(layout_node_id(node));
			const float density = context_->GetDensityIndependentPixelRatio();
			const float box_width = box->GetClientWidth() / density, box_height = box->GetClientHeight() / density;
			const auto minimum = text_scale_ / 100.f;
			const float width = std::max({node.minimum_width * minimum, node.minimum_height * minimum * node.aspect,
			                              std::min(box_width, box_height * node.aspect)});
			property(component, "position", "absolute");
			property(component, "left", std::to_string((box_width - width) / 2) + "dp");
			property(component, "top", std::to_string((box_height - width / node.aspect) / 2) + "dp");
			property(component, "width", std::to_string(width) + "dp");
			property(component, "height", std::to_string(width / node.aspect) + "dp");
		}
	for (const auto &child : node.children)
		fit_authored_aspects(child);
}
void measure_authored_geometry()
{
	if (authored_dom_.empty())
		return;
	const auto origin = document_->GetElementById("authored-body")->GetAbsoluteOffset(Rml::BoxArea::Border);
	authored_resolved_.diagnostics.clear();
	for (auto &placement : authored_resolved_.placements)
		if (auto *component = authored_component(placement.component))
		{
			placement.visible = component->IsVisible(true);
			const auto at = component->GetAbsoluteOffset(Rml::BoxArea::Border);
			placement.bounds = {at.x - origin.x, at.y - origin.y, component->GetOffsetWidth(),
			                    component->GetOffsetHeight()};
			if (placement.visible && (component->GetScrollHeight() > component->GetClientHeight() + 1 ||
			                          component->GetScrollWidth() > component->GetClientWidth() + 1))
				authored_resolved_.diagnostics.push_back(placement.id +
				                                         ": content scrolls to retain usable text and control sizes");
		}
	std::function<void(const UiNode &)> measure = [&](const UiNode &node)
	{
		auto *box = document_->GetElementById(layout_node_id(node));
		const auto at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
		authored_resolved_.containers[node.id] = {at.x - origin.x, at.y - origin.y, box->GetOffsetWidth(),
		                                          box->GetOffsetHeight()};
		for (const auto &child : node.children)
			measure(child);
	};
	measure(authored_resolved_.root);
}
void prepare_authored_layout(const Json *layout, const ConsoleRegion &region)
{
	const bool authored = layout && layout->contains("layout") && mission_outcome_.empty();
	document_->SetClass("authored", authored);
	apply_console_theme(layout);
	if (!authored)
	{
		prepare_console_tabs(region);
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
	auto geometry_key = [&]
	{
		std::string key = signature + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) + "/" +
		                  std::to_string(text_scale_) + "/" + std::to_string(model_.available_tabs().size());
		for (const auto &[group, page] : authored_pages_)
			key += "/" + group + "/" + page;
		return key;
	};
	const auto geometry = geometry_key();
	if (geometry != authored_geometry_)
	{
		if (!authored_geometry_.empty())
			cancel_pointer();
		authored_resolved_ = resolve_console_ui(*authored_program_, region.width, region.height, text_scale_ / 100.f,
		                                        authored_pages_, model_.available_tabs().size() > 1);
		for (const auto &[group, page] : authored_resolved_.selected_pages)
			authored_pages_[group] = page;
		authored_geometry_ = geometry_key();
	}
	const auto &resolved = authored_resolved_;

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
	const auto dom = signature + (resolved.compact ? "/compact" : "/wide");
	if (authored_dom_ != dom)
	{
		build_authored_tree(resolved.root);
		authored_dom_ = dom;
	}
	auto stage_started = GetTime();
	auto stage = [&](const char *name)
	{
		const auto now = GetTime();
		app_->frame_metrics["console/" + std::to_string(route_) + "/" + name] = now - stage_started;
		stage_started = now;
	};
	style_authored_tree(resolved.root);
	stage("authored-style");
	context_->Update();
	stage("authored-update-style");
	place_authored_instruments(resolved.root);
	stage("authored-instruments");
	context_->Update();
	stage("authored-update-instruments");
	fit_authored_aspects(resolved.root);
	stage("authored-aspects");
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
		const bool selected = authored_pages_.at(resolved.page_groups.at(tab.id)) == tab.id;
		markup += "<button id=\"" + id("layout-page/" + tab.id) + "\" class=\"" + (selected ? "selected" : "") + "\">" +
		          Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(id("layout-page/" + tab.id), label);
		app_->screen_toggles[id("layout-page/" + tab.id)] = selected;
	}
	if (markup != authored_tabs_markup_)
	{
		authored_tabs_markup_ = markup;
	}
	prepare_console_tabs(region);
	stage("authored-tabs");
	context_->Update();
	stage("authored-update-aspects");
	measure_authored_geometry();
	stage("authored-geometry");
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
