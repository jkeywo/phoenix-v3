// Live components are placed only by the authored layout, including Workshop previews.
std::map<std::string, std::string> station_component_markup_;
std::string station_component_scope_;
void ensure_station_components()
{
	auto *bank = document_->GetElementById("component-bank");
	if (!bank)
	{
		auto created = document_->CreateElement("div");
		created->SetId("component-bank");
		created->SetProperty("display", "none");
		bank = document_->AppendChild(std::move(created));
	}
	for (const char *component : {"power", "repair", "operations", "docking", "craft", "personnel", "away"})
		if (!authored_component(component))
		{
			auto created = document_->CreateElement("div");
			created->SetId(std::string(component) + "-panel");
			created->SetClass("station-panel", true);
			created->SetClass("engineering-board",
			                  std::string(component) == "power" || std::string(component) == "repair");
			bank->AppendChild(std::move(created));
		}
}
bool station_component_active(const std::string &component)
{
	return !settings_open_ && model_.ui_availability().features.contains(component) &&
	       authored_component_visible(component);
}
void prepare_station_components(const ConsoleRegion &region, bool pregame)
{
	if (pregame || settings_open_)
		return;
	prepare_engineering_component("power", region, pregame);
	prepare_engineering_component("repair", region, pregame);
	for (const char *component : {"operations", "docking", "craft", "personnel", "away"})
	{
		if (!station_component_active(component))
			continue;
		auto saved = std::move(local_settings_controls_);
		local_settings_controls_.clear();
		station_component_scope_ = component;
		std::string markup;
		std::vector<std::pair<std::string, std::string>> readouts;
		if (station_component_scope_ == "operations")
		{
			markup = logistics_markup();
			readouts = supply_readouts_;
		}
		if (station_component_scope_ == "docking")
		{
			markup = docking_markup();
			readouts = supply_readouts_;
		}
		if (station_component_scope_ == "craft")
		{
			markup = craft_markup();
			readouts = craft_readouts_;
		}
		if (station_component_scope_ == "personnel")
		{
			markup = personnel_markup(*app_);
			readouts = personnel_readouts_;
		}
		if (station_component_scope_ == "away")
		{
			markup = away_markup();
			readouts = away_readouts_;
		}
		station_component_scope_.clear();
		// Telemetry is updated in place so a live panel retains focus and scroll position.
		for (const auto &[key, text] : readouts)
		{
			const auto start = markup.find("id=\"" + key + "\"");
			if (start == std::string::npos)
				continue;
			const auto content = markup.find('>', start), end = markup.find("</", content);
			if (end != std::string::npos)
				markup.erase(content + 1, end - content - 1);
		}
		auto *panel = authored_component(component);
		if (station_component_markup_[component] != markup)
		{
			panel->SetInnerRML(markup);
			station_component_markup_[component] = markup;
			elements_.clear();
		}
		for (const auto &[key, text] : readouts)
		{
			if (auto *element = document_->GetElementById(key))
				leaf_markup(element, Rml::StringUtilities::EncodeRml(text));
			app_->screen_readouts.emplace_back(key, text);
		}
		for (const auto &control : local_settings_controls_)
			app_->screen_controls.push_back(control);
		context_->Update();
		for (const auto &[key, text] : readouts)
			place(*app_, region, key);
		for (const auto &[key, label] : local_settings_controls_)
			place(*app_, region, key);
		local_settings_controls_ = std::move(saved);
	}
}
