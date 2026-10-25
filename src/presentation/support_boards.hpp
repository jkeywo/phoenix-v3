// One persistent board per engineering watch. Telemetry updates never recreate a live slider.
std::map<std::string, std::string> engineering_signatures_;
std::string power_identity_;
std::weak_ptr<SessionPort> power_owner_;
std::uint64_t power_generation_ = 0;
std::map<std::string, PowerSetting> power_controls_;
struct PowerPending
{
	std::int64_t value = 0;
	std::uint64_t until = 0;
};
std::map<std::string, PowerPending> power_pending_;
std::map<std::string, std::string> support_choices_;
bool support_value(const std::string &name, const std::string &text)
{
	if (settings_open_)
		return false;
	if (name == id("support-repair-priority") && authored_component_visible("repair"))
	{
		const auto choice = support_choices_.find(text);
		return choice != support_choices_.end() && model_.act(choice->second);
	}
	if ((!authored_component_visible("power") && support_popup_ != "power") || !name.starts_with(id("power/")))
		return false;
	const auto found = power_controls_.find(name.substr(id("").size()));
	if (found == power_controls_.end())
		return false;
	double percent = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), percent);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(percent) || percent < 0 ||
	    percent > 100)
		return false;
	const auto setting = found->second;
	const auto value = setting.minimum + std::llround((setting.maximum - setting.minimum) * percent / 100);
	if (!model_.command(setting.action, value, "controller/" + setting.controller + "/" + setting.channel))
		return false;
	power_pending_[setting.id] = {value, app_->session->ticks() + 40};
	return true;
}
void prepare_engineering_component(const std::string &component, const ConsoleRegion &region, bool pregame)
{
	auto *board = authored_component(component);
	if (settings_open_ || pregame || !model_.observation() || !authored_component_visible(component))
		return;
	const auto &view = *model_.observation();
	if (power_owner_.lock() != app_->session || power_generation_ != app_->session->generation() ||
	    power_identity_ != controller_identity())
	{
		power_pending_.clear();
		power_owner_ = app_->session;
		power_generation_ = app_->session->generation();
		power_identity_ = controller_identity();
	}
	if (component == "power")
		power_controls_.clear();
	else
		support_choices_.clear();
	std::vector<std::pair<std::string, std::string>> readouts, buttons;
	std::vector<std::pair<std::string, std::string>> selects;
	auto esc = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	auto readout = [&](const std::string &key, const std::string &text)
	{
		const auto name = id(key);
		readouts.emplace_back(name, text);
		return "<span id=\"" + esc(name) + "\"></span>";
	};
	auto button = [&](const std::string &key, const std::string &label)
	{
		buttons.emplace_back(id(key), label);
		return "<button id=\"" + esc(id(key)) + "\">" + esc(label) + "</button>";
	};
	const auto settings = power_settings(view);
	auto slider = [&](const PowerSetting *setting, const std::string &label)
	{
		if (!setting)
			return std::string{};
		power_controls_[setting->id] = *setting;
		return "<label for=\"" + esc(id(setting->id)) + "\">" + esc(label) + "</label><input id=\"" +
		       esc(id(setting->id)) + "\" type=\"range\" min=\"0\" max=\"100\" step=\"0.1\" value=\"0\"/>" +
		       readout("setting/" + setting->id, std::to_string(setting->value));
	};
	std::string markup = "<h2>" + esc(localise(component == "repair" ? "Repair" : "Power")) + "</h2>";
	if (component == "repair")
	{
		const auto combat = view.gameplay.value("combat", Json::object());
		const auto parts = combat.value("compartments", Json::array());
		const auto damaged = std::count_if(parts.begin(), parts.end(),
		                                   [](const Json &part)
		                                   {
			                                   return part.value("damaged", part.at("hp") < part.at("maximum"));
		                                   });
		markup += "<p>" +
		          readout("support/repair/summary", localise("Damaged sections") + ": " + std::to_string(damaged) +
		                                                " / " + std::to_string(parts.size())) +
		          "</p>";
		// High-level repair priorities remain directly on the board at assisted workloads.
		std::vector<Json> priorities;
		for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
			if (descriptor.value("action", "") == "intent_integer" && support_repair_control({}, descriptor))
				priorities.push_back(descriptor);
		if (!priorities.empty())
		{
			markup += "<label for=\"" + id("support-repair-priority") + "\">" + esc(localise("Repair priority")) +
			          "</label><select id=\"" + id("support-repair-priority") + "\">";
			std::string current;
			for (const auto &choice : priorities)
			{
				const auto key = std::to_string(choice.at("value").get<std::int64_t>());
				const auto label = localise(choice.value("label_key", choice.value("label", key)));
				support_choices_[key] = descriptor_action(choice.at("id"));
				bool selected = false;
				for (const auto &controller : view.gameplay.at("controllers"))
					if (controller.at("id") == choice.at("controller"))
						selected = controller.at("commands").at(choice.at("channel").get<std::string>()).at("value") ==
						           choice.at("value");
				markup +=
				    "<option value=\"" + key + "\"" + (selected ? " selected" : "") + ">" + esc(label) + "</option>";
				if (selected)
					current = label;
			}
			markup += "</select>";
			selects.emplace_back(id("support-repair-priority"), current);
		}
		markup += "<div class=\"repair-columns\"><div><h3>" + esc(localise("Sections")) + "</h3>";
		for (const auto &part : parts)
		{
			const auto key = part.at("id").get<std::string>();
			const auto condition =
			    part.at("hp").get<std::int64_t>() * 100 / std::max<std::int64_t>(1, part.at("maximum"));
			markup += "<p>" +
			          readout("support/repair/section/" + key,
			                  localise(engineering_name(key)) + " — " +
			                      localise(part.value("damaged", false) ? "Damaged" : "Operational") + " (" +
			                      std::to_string(condition) + "%)") +
			          "</p>";
		}
		markup += "</div><div><h3>" + esc(localise("Teams")) + "</h3>";
		for (const auto &team : view.gameplay.value("teams", Json::array()))
			if (team.value("repair", false))
			{
				const auto key = team.at("id").get<std::string>();
				auto location = [&](std::int64_t index)
				{
					for (const auto &part : parts)
						if (part.at("index").get<std::int64_t>() == index + 1)
							return localise(engineering_name(part.at("id")));
					return localise("Unknown");
				};
				const auto text =
				    localise(engineering_name(key)) + " — " + localise(team.value("work", "Idle")) +
				    (team.value("work_system", "").empty()
				         ? ""
				         : ": " + localise(engineering_name(team.at("work_system")))) +
				    " | " + location(team.at("location")) +
				    (team.value("work", "") == "Travelling" ? " → " + location(team.at("destination")) : "");
				const auto action = "support-details/team/" + key;
				buttons.emplace_back(id(action), text);
				markup += "<button id=\"" + esc(id(action)) + "\">" + readout("support/repair/team/" + key, text) +
				          "</button>";
			}
		markup += "</div></div>";
	}
	else
	{
		const auto power = view.gameplay.value("power", Json::object());
		std::int64_t energy = 0, capacity = 0, remaining = 0, coolant_capacity = 0;
		for (const auto &reserve : power.value("reserves", Json::array()))
		{
			energy += reserve.at("energy").get<std::int64_t>();
			capacity += reserve.at("capacity").get<std::int64_t>();
		}
		for (const auto &source : power.value("coolant", Json::array()))
		{
			remaining += source.at("remaining").get<std::int64_t>();
			coolant_capacity += source.at("capacity").get<std::int64_t>();
		}
		markup += "<div class=\"power-reserves\"><p>" +
		          readout("support/power/battery", localise("Battery reserves") + ": " + std::to_string(energy) +
		                                               " / " + std::to_string(capacity)) +
		          "</p><p>" +
		          readout("support/power/coolant", localise("Coolant reserves") + ": " + std::to_string(remaining) +
		                                               " / " + std::to_string(coolant_capacity)) +
		          "</p>";
		for (const auto &reactor : power.value("reactors", Json::array()))
		{
			const auto key = reactor.at("id").get<std::string>();
			const PowerSetting *setting = nullptr;
			for (const auto &candidate : settings)
				if (candidate.subject == key && !candidate.coolant)
					setting = &candidate;
			markup += "<div><h3>" + esc(localise(engineering_name(key))) + "</h3><p>" +
			          readout("support/power/reactor/" + key,
			                  localise("Output") + ": " + std::to_string(reactor.at("output").get<std::int64_t>()) +
			                      " / " + std::to_string(reactor.at("maximum").get<std::int64_t>())) +
			          "</p>" + slider(setting, localise("Reactor output (%)")) + "</div>";
		}
		markup += "</div>";
		const bool groups = !power.value("groups", Json::array()).empty();
		markup += "<h3>" + esc(localise(groups ? "Power groups" : "Systems")) + "</h3>";
		std::set<std::string> placed;
		auto rows = power.value(groups ? "groups" : "systems", Json::array());
		const float scale = text_scale_ / 100.f;
		const float room = board->GetClientHeight() / context_->GetDensityIndependentPixelRatio() - 170.f * scale;
		const auto limit =
		    static_cast<std::size_t>(std::max(2.f, std::floor(room / ((settings.empty() ? 55.f : 80.f) * scale))));
		std::size_t count = 0;
		for (const auto &row : rows)
		{
			if (groups && !row.value("parent", "").empty())
				continue;
			if (count++ >= limit)
				continue;
			const auto key = row.at("id").get<std::string>();
			const bool detail_only = groups && row.contains("group");
			auto *allocation = detail_only ? nullptr : power_setting_for(settings, row, false);
			auto *cooling = detail_only ? nullptr : power_setting_for(settings, row, true);
			const bool shared = allocation && !placed.insert(allocation->id).second;
			if (shared)
				allocation = nullptr;
			if (cooling && !placed.insert(cooling->id).second)
				cooling = nullptr;
			markup += "<div class=\"power-row\"><div class=\"power-name\">" + esc(localise(engineering_name(key))) +
			          "</div><div class=\"power-control\">" +
			          (shared ? "<span>" + esc(localise("Shared allocation")) + "</span>"
			                  : slider(allocation, localise("Power (%)"))) +
			          readout("support/power/allocation/" + key,
			                  localise("Power") + ": " + std::to_string(row.value("allocation", 0LL)) + " / s") +
			          "</div>";
			if (cooling)
				markup += "<div class=\"power-control\">" + slider(cooling, localise("Coolant (%)")) + "</div>";
			markup += "<div class=\"power-battery\">" +
			          readout("support/power/battery/" + key,
			                  localise("Battery") + ": " + std::to_string(row.at("energy").get<std::int64_t>()) +
			                      " / " + std::to_string(row.at("capacity").get<std::int64_t>())) +
			          "</div></div>";
		}
		markup += button("power-details", localise("Power details"));
	}
	if (markup != engineering_signatures_[component])
	{
		syncing_values_ = true;
		board->SetInnerRML(markup);
		syncing_values_ = false;
		elements_.clear();
		properties_.clear();
		engineering_signatures_[component] = markup;
	}
	for (const auto &[name, text] : readouts)
	{
		if (auto *element = document_->GetElementById(name))
			leaf_markup(element, esc(text));
		app_->screen_readouts.emplace_back(name, text);
	}
	for (const auto &[name, label] : buttons)
		app_->screen_controls.emplace_back(name, label);
	for (const auto &[name, value] : selects)
		app_->screen_inputs.push_back({name, localise("Repair priority"), value, false});
	syncing_values_ = true;
	for (const auto &[key, setting] : component == "power" ? power_controls_ : std::map<std::string, PowerSetting>{})
	{
		auto value = setting.value;
		if (auto pending = power_pending_.find(key); pending != power_pending_.end())
		{
			if (pending->second.value != setting.value && app_->session->ticks() < pending->second.until)
				value = pending->second.value;
			else
				power_pending_.erase(pending);
		}
		const auto percent =
		    std::clamp(100.0 * (value - setting.minimum) / std::max<std::int64_t>(1, setting.maximum - setting.minimum),
		               0.0, 100.0);
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id(key))))
			if (field->GetValue() != std::to_string(percent))
				field->SetValue(std::to_string(percent));
		app_->screen_inputs.push_back({id(key),
		                               localise(setting.coolant ? "Coolant (%)" : "Power (%)") + " — " +
		                                   localise(engineering_name(setting.subject)),
		                               std::to_string(percent), true});
	}
	syncing_values_ = false;
	context_->Update();
	for (const auto &[name, text] : readouts)
		place(*app_, region, name);
	for (const auto &[name, label] : buttons)
		place(*app_, region, name);
	for (const auto &[name, value] : selects)
		place(*app_, region, name);
	for (const auto &[key, setting] : component == "power" ? power_controls_ : std::map<std::string, PowerSetting>{})
		place(*app_, region, id(key));
}
