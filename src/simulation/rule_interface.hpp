// Included inside World::Impl. Publish automatic rule status through the same
// Console feeds and personnel departments as the native source's other data.
Json rules_board(const ConsoleAddress &address) const
{
	Json result = Json::array();
	const auto found = entities.find(address.ship);
	const auto *console = seat(address.ship, address.console);
	if (found == entities.end() || !console)
		return result;
	const auto ship = found->second;
	const auto &feeds = console->feed_systems;
	const auto equipment_visible = [&](const std::string &equipment)
	{
		return !equipment.empty() && std::find(feeds.begin(), feeds.end(), equipment) != feeds.end();
	};
	const auto officer_visible = [&](const std::string &officer)
	{
		for (const auto &definition : this->definition(address.ship).personnel.officers)
			if (definition.id == officer)
				return personnel_owns(address, definition.department);
		return false;
	};
	std::map<std::pair<std::string, std::string>, const RuleExecutionState *> states;
	if (const auto *runtime = ship.try_get<RuleRuntime>())
		for (const auto &[key, state] : runtime->executions)
			states.emplace(std::pair{state.source_key, state.binding}, &state);
	std::map<std::uint64_t, const ActiveRuleEffect *> active;
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.context.target == address.ship && !effect.context.ordnance)
			active.emplace(effect.id, &effect);
	}
	for (const auto &source : rule_sources(ship))
	{
		if (!source.bindings || source.owner.ordnance)
			continue;
		if ((!source.owner.officer.empty() && !officer_visible(source.owner.officer)) ||
		    (!source.owner.equipment.empty() && !equipment_visible(source.owner.equipment)))
			continue;
		std::string kind;
		switch (source.kind)
		{
		case RuleOwnerKind::Equipment:
			kind = "equipment";
			break;
		case RuleOwnerKind::Officer:
			kind = "officer";
			break;
		case RuleOwnerKind::Effect:
			kind = "effect";
			break;
		case RuleOwnerKind::Region:
			kind = "region";
			break;
		case RuleOwnerKind::Shield:
			kind = "shield";
			break;
		}
		for (const auto &binding : *source.bindings)
		{
			const auto definition = rule_templates.find(binding.rule);
			if (definition == rule_templates.end())
				continue;
			const RuleExecutionState *state = nullptr;
			if (source.active_effect)
			{
				if (const auto effect = active.find(source.active_effect); effect != active.end())
					for (const auto &[key, clock] : effect->second->clocks)
						if (clock.binding == binding.id && clock.definition == binding.rule)
						{
							state = &clock;
							break;
						}
			}
			else if (const auto at = states.find({source.key, binding.id}); at != states.end())
				state = at->second;
			std::string phase = "ready", blocker;
			std::int64_t output = 0;
			if (!source.eligible)
			{
				phase = "unavailable";
				blocker = "source";
			}
			else if (state)
			{
				output = std::clamp(state->output, std::int64_t{0}, std::int64_t{1000});
				if (state->stopped)
				{
					phase = "stopped";
					blocker = "stopped";
				}
				else if (!state->eligible)
					blocker = "conditions";
				else if (state->active || output)
					phase = output ? "active" : "paused";
				if (!state->stopped && !state->blocker.empty())
				{
					if (state->blocker.starts_with("Invalid rule arithmetic"))
						blocker = "expression";
					else
						blocker = definition->second.funding ? "funding" : "target";
					if (phase == "ready")
						phase = "paused";
				}
			}
			// Logical keys are already one-way opaque hashes; source lineage and
			// captured target/fact data never enter the public descriptor.
			result.push_back({{"id", state ? state->key : rule_key(source.key, binding.id, source.context.subject)},
			                  {"rule", binding.rule},
			                  {"label", definition->second.label},
			                  {"source_kind", kind},
			                  {"equipment", source.owner.equipment},
			                  {"phase", phase},
			                  {"output", output},
			                  {"blocker", blocker}});
			if (result.size() == 4096)
				return result;
		}
	}
	return result;
}
