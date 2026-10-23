// Only published ship objectives are eligible. Private Console/scenario records never enter this list.
std::map<KnowledgeId, KnowledgeRecord> ship_objectives(EntityId ship) const
{
	std::map<KnowledgeId, KnowledgeRecord> result;
	if (!gameplay)
		return result;
	const auto &seats = definition(ship).consoles;
	if (seats.empty())
		return result;
	const auto board = knowledge.project({ship, seats.front().id}, true);
	const auto key = knowledge.definition().schema("phoenix.objective");
	for (const auto &record : board.shared)
		if (record.value.schema == key)
			result[record.value.subject] = record;
	std::erase_if(result,
	              [&](const auto &row)
	              {
		              return row.second.value.withdrawn || knowledge.stale(row.second, ecs.get<Clock>().tick);
	              });
	return result;
}
bool active_objective(const KnowledgeRecord &record) const
{
	const auto field = knowledge.definition().field(record.value.schema, "status");
	const auto found = record.value.fields.find(field);
	return found != record.value.fields.end() && std::get_if<std::string>(&found->second) &&
	       std::get<std::string>(found->second) == "active";
}
bool ship_has_ai(EntityId ship) const
{
	const auto *controls = entities.at(ship).try_get<EntityControls>();
	return controls && std::any_of(controls->controllers.begin(), controls->controllers.end(),
	                               [](const auto &controller)
	                               {
		                               return !controller.human;
	                               });
}
void reconcile_objective_priority(EntityId ship)
{
	auto &state = entities.at(ship).get_mut<BridgeBlackboard>();
	if (!state.priority_objective)
		return;
	const auto objectives = ship_objectives(ship);
	if (!objectives.contains(state.priority_objective) || !active_objective(objectives.at(state.priority_objective)))
	{
		state.priority_objective = 0;
		++state.priority_revision;
	}
}
Json objectives_board(EntityId ship) const
{
	const auto &state = entities.at(ship).get<BridgeBlackboard>();
	Json result{{"selected", state.priority_objective},
	            {"revision", state.priority_revision},
	            {"can_select", ship_has_ai(ship)},
	            {"objectives", Json::array()}};
	const auto &schema = knowledge.definition();
	for (const auto &[id, record] : ship_objectives(ship))
	{
		Json row;
		for (const auto *name : {"id", "title", "description", "status", "destination_x", "destination_y"})
		{
			const auto value = record.value.fields.find(schema.field(record.value.schema, name));
			if (value != record.value.fields.end())
				std::visit(
				    [&](const auto &v)
				    {
					    row[name] = v;
				    },
				    value->second);
		}
		row["priority"] = id == state.priority_objective;
		result["objectives"].push_back(std::move(row));
	}
	return result;
}
// Shipwide assessment exposes no private contacts, telemetry or machinery commands.
Json navigation_board(EntityId ship) const
{
	const auto &evidence = entities.at(ship).get<BridgeBlackboard>().navigation;
	return {{"observed_tick", evidence.observed_tick},
	        {"blocked", evidence.blocked},
	        {"left_heading", evidence.left_heading},
	        {"lateral_thrust", evidence.lateral_thrust},
	        {"lateral_safe", evidence.lateral_safe}};
}
// Included inside World::Impl. All reads use accepted intent, including AI reads.
bool stance_target(EntityId ship, const std::string &console) const
{
	const auto &data = definition(ship);
	const auto found = data.console_bridge.find(console);
	if (found == data.console_bridge.end() || found->second.command || found->second.stances.empty())
		return false;
	const auto *controls = entities.at(ship).try_get<EntityControls>();
	if (!controls)
		return false;
	bool any = false;
	for (const auto &controller : controls->controllers)
		if (controller.definition.console == console)
		{
			any = true;
			if (controller.human)
				return false;
		}
	return any;
}
bool bridge_visible(const ConsoleAddress &address) const
{
	const auto &data = definition(address.ship);
	const auto found = data.console_bridge.find(address.console);
	if (found == data.console_bridge.end() || !found->second.auto_hide)
		return true;
	for (const auto &[console, spec] : data.console_bridge)
		if (stance_target(address.ship, console))
			return true;
	return false;
}
Json alert_board(EntityId ship) const
{
	const auto &spec = definition(ship).bridge;
	if (spec.conditions.empty())
		return Json::object();
	const auto &state = entities.at(ship).get<BridgeBlackboard>();
	const auto &choice = spec.conditions.at(state.condition);
	return {
	    {"id", choice.id},         {"label", choice.label}, {"severity", state.condition}, {"revision", state.revision},
	    {"colour", choice.colour}, {"glow", choice.glow},   {"siren", choice.siren},       {"music", choice.music}};
}
Json stance_board(const ConsoleAddress &address) const
{
	const auto &state = entities.at(address.ship).get<BridgeBlackboard>();
	if (!state.stances.contains(address.console))
		return Json::object();
	const auto &choice =
	    definition(address.ship).console_bridge.at(address.console).stances.at(state.stances.at(address.console));
	return {{"id", choice.id}, {"label", choice.label}};
}
bool bridge_command(const ConsoleAddress &source, const OperatingCommand &command, bool execute)
{
	if (!command.controller.empty() || !command.channel.empty() || command.request_id || !command.arguments.empty() ||
	    command.value < 0)
		return false;
	const auto &data = definition(source.ship);
	auto &state = entities.at(source.ship).get_mut<BridgeBlackboard>();
	if (command.action == "objective.priority")
	{
		if (!data.console_bridge.at(source.console).objectives || !ship_has_ai(source.ship) ||
		    !command.system.empty() || !command.text.empty() || command.generation != state.priority_revision)
			return false;
		const auto objectives = ship_objectives(source.ship);
		if (command.value && (!objectives.contains(command.value) || !active_objective(objectives.at(command.value))))
			return false;
		if (execute && state.priority_objective != static_cast<std::uint64_t>(command.value))
		{
			state.priority_objective = command.value;
			++state.priority_revision;
		}
		return true;
	}
	if (command.action == "alert.set")
	{
		if (source.console != data.bridge.alert_console || !command.system.empty() || command.generation ||
		    static_cast<std::size_t>(command.value) >= data.bridge.conditions.size())
			return false;
		if (execute && state.condition != static_cast<std::size_t>(command.value))
		{
			if (state.revision >= 9007199254740991ULL)
				return false;
			state.condition = static_cast<std::size_t>(command.value);
			++state.revision;
		}
		return true;
	}
	const auto target = command.system.empty() ? source.console : command.system;
	const auto found = data.console_bridge.find(target);
	if (found == data.console_bridge.end() || static_cast<std::size_t>(command.value) >= found->second.stances.size())
		return false;
	if (target != source.console &&
	    (!data.console_bridge.contains(source.console) || !data.console_bridge.at(source.console).command ||
	     !stance_target(source.ship, target) || command.generation != state.epochs.at(target)))
		return false;
	if (execute)
		state.stances.at(target) = static_cast<std::size_t>(command.value);
	return true;
}
void bridge_controls(const ConsoleAddress &address, Json &buttons) const
{
	const auto &data = definition(address.ship);
	if (data.console_bridge.at(address.console).objectives && ship_has_ai(address.ship))
	{
		const auto &state = entities.at(address.ship).get<BridgeBlackboard>();
		const auto add_priority = [&](std::uint64_t id, const std::string &title)
		{
			buttons.push_back({{"id", "objective.priority." + std::to_string(id)},
			                   {"action", "objective.priority"},
			                   {"value", id},
			                   {"system", ""},
			                   {"label", title},
			                   {"label_prefix", id ? "Prioritise objective: " : ""},
			                   {"selected", id == state.priority_objective},
			                   {"generation", state.priority_revision}});
		};
		for (const auto &[id, record] : ship_objectives(address.ship))
			if (active_objective(record))
				add_priority(id, std::get<std::string>(record.value.fields.at(
				                     knowledge.definition().field(record.value.schema, "title"))));
		if (state.priority_objective)
			add_priority(0, "Clear priority objective");
	}
	const auto add =
	    [&](const std::string &action, const std::string &target, const auto &choices, const std::string &prefix)
	{
		for (std::size_t i = 0; i < choices.size(); ++i)
			buttons.push_back(
			    {{"id", action + "." + target + "." + choices[i].id},
			     {"label", choices[i].label},
			     {"label_prefix", prefix},
			     {"action", action},
			     {"system", target},
			     {"value", i},
			     {"selected", action == "alert.set"
			                      ? entities.at(address.ship).get<BridgeBlackboard>().condition == i
			                      : entities.at(address.ship).get<BridgeBlackboard>().stances.at(target) == i}});
		if (action == "stance.set")
			for (auto &button : buttons)
				if (button.at("action") == action && button.at("system") == target)
					button["generation"] = entities.at(address.ship).get<BridgeBlackboard>().epochs.at(target);
	};
	if (data.bridge.alert_console == address.console)
		add("alert.set", "", data.bridge.conditions, "");
	for (const auto &[console, spec] : data.console_bridge)
		if (console == address.console ||
		    (data.console_bridge.at(address.console).command && stance_target(address.ship, console)))
			add("stance.set", console, spec.stances, console + ": ");
}
void restore_bridge(EntityId ship, const Json &saved)
{
	auto &state = entities.at(ship).get_mut<BridgeBlackboard>();
	const auto &data = definition(ship);
	const auto condition = saved.at("condition").get<std::size_t>();
	if (condition >= std::max<std::size_t>(1, data.bridge.conditions.size()))
		throw std::runtime_error("Saved alert condition outside authored list");
	const auto stances = saved.at("stances").get<std::map<std::string, std::size_t>>();
	if (stances.size() != state.stances.size())
		throw std::runtime_error("Saved stance roster differs from Consoles");
	for (const auto &[console, choice] : stances)
		if (!state.stances.contains(console) || choice >= data.console_bridge.at(console).stances.size())
			throw std::runtime_error("Saved stance outside authored list");
	const auto revision = display::number(saved.at("revision"), 0, 9007199254740991LL);
	auto epochs = state.epochs;
	if (saved.contains("epochs"))
	{
		if (saved.at("epochs").size() != epochs.size())
			throw std::runtime_error("Saved stance authority roster differs");
		for (auto &[console, epoch] : epochs)
			epoch = display::number(saved.at("epochs").at(console), 0, 9007199254740991LL);
	}
	state = {condition, static_cast<std::uint64_t>(revision), stances, epochs};
	state.priority_objective = display::number(saved.value("priority_objective", Json(0)), 0, 9007199254740991LL);
	state.priority_revision = display::number(saved.value("priority_revision", Json(0)), 0, 9007199254740991LL);
	if (saved.contains("navigation"))
	{
		const auto &value = saved.at("navigation");
		state.navigation = {
		    static_cast<std::uint64_t>(display::number(value.at("observed_tick"), 0, 9007199254740991LL)),
		    value.at("blocked").get<bool>(), value.at("lateral_safe").get<bool>(),
		    display::number(value.at("left_heading"), 0, 359999),
		    display::number(value.at("lateral_thrust"), -1000, 1000)};
	}
}
