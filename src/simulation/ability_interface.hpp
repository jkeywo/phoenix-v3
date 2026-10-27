// Included inside World::Impl. Control and continuation use the same compiled
// endpoints and source identities as physical ability execution.
bool ability_channel_bound(EntityId ship, std::size_t controller, std::size_t channel) const
{
	for (const auto &control : definition(ship).abilities.controls)
		if ((control.controller == controller && (control.enabled == channel || control.target == channel)) ||
		    (control.planner && *control.planner == controller &&
		     (control.planning_enabled == channel || control.planning_target == channel)))
			return true;
	return false;
}

const AbilityGrantState *ability_control_grant(EntityId ship, const AbilityControlDefinition &control) const
{
	const auto entity = entities.find(ship);
	const auto *runtime = entity == entities.end() ? nullptr : entity->second.try_get<AbilityRuntime>();
	if (!runtime)
		return nullptr;
	const AbilityGrantState *result = nullptr;
	for (const auto &[id, grant] : runtime->grants)
		if (const auto *resolved = resolve_ability_control(ship, grant); resolved && resolved->id == control.id)
		{
			// Distinct lifetime grants of one ability share its control endpoint.
			// Prefer the currently executing instance, then the stable first grant.
			if (!result || (result->phase == "ready" && grant.phase != "ready"))
				result = &grant;
		}
	return result;
}

bool ability_generation_valid(EntityId ship, std::size_t controller, std::size_t channel,
                              std::span<const std::int64_t> arguments) const
{
	if (!ability_channel_bound(ship, controller, channel))
		return arguments.empty();
	if (arguments.empty())
		return true; // Ordinary Controller trees address the current binding.
	if (arguments.size() != 1 || arguments.front() <= 0)
		return false;
	for (const auto &control : definition(ship).abilities.controls)
	{
		if (control.planner && *control.planner == controller &&
		    (control.planning_enabled == channel || control.planning_target == channel))
			return false; // Planning preferences do not carry an execution grant.
		if (control.controller == controller && (control.enabled == channel || control.target == channel))
		{
			const auto *grant = ability_control_grant(ship, control);
			return grant && grant->generation == static_cast<std::uint64_t>(arguments.front());
		}
	}
	return false;
}

bool ability_channel_valid(EntityId ship, std::size_t controller, std::size_t channel, std::int64_t value,
                           std::uint64_t request_id = 0) const
{
	ConsoleContacts contacts;
	for (const auto &control : definition(ship).abilities.controls)
	{
		const bool planning = control.planner && *control.planner == controller;
		const bool enabled = (planning ? control.planning_enabled : control.enabled) == channel;
		const bool target = (planning ? control.planning_target : control.target) == channel;
		if ((!planning && control.controller != controller) || (!enabled && !target))
			continue;
		if (enabled && value != 0 && value != 1)
			return false;
		if (target && value < 0)
			return false;
		if (!value)
			return true;
		const auto *grant = ability_control_grant(ship, control);
		if (!grant)
			return planning && enabled;
		const auto &ability = ability_definition(ship, *grant);
		if (target)
			return ability_target_valid(ship, control, ability, static_cast<EntityId>(value), contacts);
		if (planning)
			return true;
		if (grant->expiry >= 0 && grant->expiry <= static_cast<std::int64_t>(ecs.get<Clock>().tick))
			return false;
		const auto entity = entities.at(ship);
		const auto &commands = entity.get<EntityControls>().controllers.at(control.controller).commands;
		const auto &command = commands.at(channel);
		if (request_id && command.request_id == request_id && command.accepted && command.value == value)
			return true; // Replay returns its existing receipt, without new work.
		auto candidate = *grant;
		candidate.control = control.id;
		candidate.apparatus =
		    control.apparatus.empty() && candidate.source == "equipment" ? candidate.owner : control.apparatus;
		// A sustained setting is requested intent, not a paid start. Controllers
		// may set enable before target, and retain it through recovery/shortage.
		if (ability.sustained)
			return grant->phase != "ready" || ability_conflict(ship, candidate).empty();
		if (grant->phase != "ready")
			return false;
		if (ability_recovery_until(ship, candidate) > ecs.get<Clock>().tick ||
		    !ability_conflict(ship, candidate).empty() || !ability_apparatus_blocker(ship, candidate).empty())
			return false;
		const auto selected =
		    control.target == ability_no_channel ? ship : static_cast<EntityId>(commands.at(control.target).value);
		return ability_blocker(ship, control, candidate, ability.target == "self" ? ship : selected, contacts).empty();
	}
	return true;
}

bool ability_command_valid(EntityId ship, std::size_t controller, std::size_t channel,
                           const OperatingCommand &command) const
{
	if (!ability_channel_bound(ship, controller, channel))
		return true;
	return ability_generation_valid(ship, controller, channel, command.arguments) &&
	       ability_channel_valid(ship, controller, channel, command.value, command.request_id);
}

bool ability_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller, std::size_t channel,
                      Json &buttons, ConsoleContacts &contacts) const
{
	const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
	for (const auto &control : definition(address.ship).abilities.controls)
	{
		const auto &execution = controllers.at(control.controller);
		if (execution.definition.console != address.console)
			continue;
		const bool planning = control.planner && &controller == &controllers.at(*control.planner);
		if (!planning && &controller != &execution)
			continue;
		const bool enabled = channel == (planning ? control.planning_enabled : control.enabled);
		const bool target = channel == (planning ? control.planning_target : control.target);
		if (!enabled && !target)
			continue;
		const auto *grant = ability_control_grant(address.ship, control);
		if (!grant)
			return true; // Suppress raw intent controls for an unavailable grant.
		const auto &ability = ability_definition(address.ship, *grant);
		std::vector<std::pair<std::int64_t, std::string>> choices;
		if (enabled)
		{
			if (!ability.sustained)
			{
				const bool preparing =
				    grant->phase == "preparing" || (grant->phase == "paused" && grant->resume_phase == "preparing");
				choices.emplace_back(preparing ? 0 : 1, preparing ? "Cancel ability preparation" : "Activate ability");
			}
			else
			{
				const auto requested = controller.commands.at(channel).value != 0;
				choices.emplace_back(requested ? 0 : 1, requested ? "Stop ability" : "Activate ability");
			}
		}
		else if (ability.target != "self")
		{
			choices.emplace_back(0, "Clear ability target");
			if (ability_target_valid(address.ship, control, ability, address.ship, contacts))
				choices.emplace_back(address.ship, "Target own ship");
			const auto &observation = console_detection(address, contacts);
			for (const auto &contact : observation.contacts)
				if (contact.id != address.ship &&
				    ability_target_valid(address.ship, control, ability, contact.id, contacts))
					choices.emplace_back(contact.id, "Target");
		}
		for (const auto &[value, label] : choices)
		{
			OperatingCommand command{
			    address.ship,
			    std::string(operating_action(controller.definition.commands.at(channel).action).name),
			    value,
			    {},
			    controller.definition.id,
			    controller.definition.commands.at(channel).id,
			    controller.generation};
			if (!planning)
				command.arguments = {static_cast<std::int64_t>(grant->generation)};
			if (controller.definition.commands.at(channel).semantics == CommandSemantics::Request)
			{
				if (controller.commands.at(channel).request_id >= 9007199254740991ULL)
					continue;
				command.request_id = controller.commands.at(channel).request_id + 1;
			}
			auto button = encode_operating_command(command);
			button["id"] = "ability." + control.id + "." + controller.definition.commands.at(channel).id + "." +
			               std::to_string(value);
			button["label"] = label;
			button["ability_label"] = ability.label;
			if (target && value)
				button["ability_target"] = value;
			buttons.push_back(std::move(button));
		}
		return true;
	}
	return false;
}

struct AbilityStatus
{
	std::string phase = "unavailable", blocker = "Ability source unavailable";
	std::uint64_t deadline = 0, recovery = 0;
	std::int64_t preparation = 0, output = 0;
	bool ready = false, blocked = true, requested = false;
};

AbilityStatus ability_status(EntityId ship, const AbilityControlDefinition &control, const AbilityGrantState *grant,
                             ConsoleContacts &contacts) const
{
	AbilityStatus status;
	const auto &controller = entities.at(ship).get<EntityControls>().controllers.at(control.controller);
	const auto &intent = controller.commands.at(control.enabled);
	status.requested =
	    controller.definition.commands.at(control.enabled).semantics == CommandSemantics::Request
	        ? intent.consumed < intent.revision || (grant && (ability_running(*grant) || grant->retained_request))
	        : intent.value != 0;
	if (!grant)
		return status;
	const auto &ability = ability_definition(ship, *grant);
	auto candidate = *grant;
	candidate.control = control.id;
	candidate.apparatus =
	    control.apparatus.empty() && candidate.source == "equipment" ? candidate.owner : control.apparatus;
	status.phase = grant->phase;
	status.blocker = grant->blocker;
	status.preparation = grant->progress;
	status.output = grant->output_permille;
	const auto tick = ecs.get<Clock>().tick;
	status.deadline = ability_recovery_until(ship, candidate);
	status.recovery = status.deadline > tick ? status.deadline - tick : 0;
	if (status.phase == "ready")
	{
		const auto target = ability.target == "self" ? ship
		                    : control.target == ability_no_channel
		                        ? 0
		                        : static_cast<EntityId>(controller.commands.at(control.target).value);
		status.blocker = ability_blocker(ship, control, candidate, target, contacts);
		if (grant->expiry >= 0 && grant->expiry <= static_cast<std::int64_t>(tick))
			status.blocker = "Ability source unavailable";
		if (status.blocker.empty())
			status.blocker = ability_apparatus_blocker(ship, candidate);
		if (status.blocker.empty())
			status.blocker = ability_conflict(ship, candidate);
		if (status.blocker.empty() && !ability_recovery_capacity(ship, candidate))
			status.blocker = "Ability recovery capacity exhausted";
		if (status.recovery)
		{
			status.phase = "recovering";
			if (status.blocker.empty())
				status.blocker = "Ability recovering";
		}
		else if (!status.blocker.empty())
			status.phase = "unavailable";
	}
	status.blocked = !status.blocker.empty();
	status.ready = status.phase == "ready" && !status.blocked;
	return status;
}

Json execution_observed_properties(const RuleActor &actor, std::span<const std::string> observations) const
{
	Json rows = Json::array();
	for (const auto &id : observations)
	{
		const auto value = read_custom_property(actor, id);
		if (!value)
			continue;
		const auto &definition = property_catalogue->at(id);
		const auto kind = definition.kind == CustomPropertyKind::Quantity    ? "quantity"
		                  : definition.kind == CustomPropertyKind::Attribute ? "attribute"
		                  : definition.kind == CustomPropertyKind::Flag      ? "flag"
		                                                                     : "enum";
		Json row{{"id", id},
		         {"label", definition.label},
		         {"kind", kind},
		         {"unit", definition.unit},
		         {"display_scale", definition.display_scale}};
		if (definition.kind == CustomPropertyKind::Flag)
			row["value"] = bool(value->number);
		else if (definition.kind == CustomPropertyKind::Enum)
		{
			row["value"] = value->text;
			row["choice_label"] = definition.choices.at(value->text);
		}
		else
			row["value"] = value->number;
		rows.push_back(std::move(row));
	}
	return rows;
}

Json ability_observed_properties(EntityId ship, const AbilityGrantState &grant) const
{
	RuleActor actor{ship};
	if (grant.source == "officer")
		actor.officer = grant.owner;
	else
		actor.equipment = grant.owner;
	return execution_observed_properties(actor, ability_definition(ship, grant).observations);
}

Json ability_decision_facts(const ConsoleAddress &address, const Observation &permitted) const
{
	Json result = Json::object();
	const auto &controls = definition(address.ship).abilities.controls;
	// Passive bodies also have observation Consoles, but no EntityControls component.
	if (controls.empty())
		return result;
	const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
	if (std::none_of(controls.begin(), controls.end(),
	                 [&](const auto &control)
	                 {
		                 return controllers.at(control.controller).definition.console == address.console;
	                 }))
		return result;
	ConsoleContacts contacts;
	// Reuse the snapshot's permitted contacts. No second physical visibility
	// pass and no access to authoritative enemy state is needed for readiness.
	contacts[address].contacts = permitted.contacts;
	contacts[address].bearing_contacts = permitted.bearing_contacts;
	for (const auto &control : controls)
	{
		if (controllers.at(control.controller).definition.console != address.console)
			continue;
		const auto status =
		    ability_status(address.ship, control, ability_control_grant(address.ship, control), contacts);
		result[control.id] = {
		    {"phase", status.phase},        {"ready", status.ready},   {"preparation", status.preparation},
		    {"recovery", status.recovery},  {"output", status.output}, {"blocked", status.blocked},
		    {"requested", status.requested}};
		if (const auto *grant = ability_control_grant(address.ship, control))
			result[control.id]["properties"] = ability_observed_properties(address.ship, *grant);
	}
	return result;
}

Json ability_board(const ConsoleAddress &address) const
{
	Json out = Json::array();
	const auto entity = entities.at(address.ship);
	const auto *runtime = entity.try_get<AbilityRuntime>();
	if (!runtime)
		return out;
	ConsoleContacts contacts;
	for (const auto &[id, grant] : runtime->grants)
	{
		const auto *control = resolve_ability_control(address.ship, grant);
		if (!control)
			continue;
		const auto &controller = entity.get<EntityControls>().controllers.at(control->controller);
		if (controller.definition.console != address.console)
			continue;
		const auto &ability = ability_definition(address.ship, grant);
		const auto status = ability_status(address.ship, *control, &grant, contacts);
		out.push_back({{"id", id},
		               {"grant", id},
		               {"control", control->id},
		               {"ability", grant.definition},
		               {"label", ability.label},
		               {"source", grant.source},
		               {"owner", grant.owner},
		               {"phase", status.phase},
		               {"requested", status.requested},
		               {"target", grant.target},
		               {"progress", grant.progress},
		               {"preparation_ticks", ability.preparation_ticks},
		               {"ready", status.deadline},
		               {"blocker", status.blocker},
		               {"output", grant.output_permille},
		               {"generation", grant.generation},
		               {"energy_per_second", ability.energy_per_second},
		               {"preparation_energy_per_second", ability.preparation_energy_per_second},
		               {"properties", ability_observed_properties(address.ship, grant)}});
	}
	return out;
}

Json save_abilities() const
{
	Json out = {{"ships", Json::array()}};
	for (const auto &[id, entity] : entities)
		if (const auto *runtime = entity.try_get<AbilityRuntime>())
			out["ships"].push_back({{"ship", id}, {"state", *runtime}});
	return out;
}

void validate_ability_runtime(EntityId ship, const AbilityRuntime &state, const AbilityRuntime &initial) const
{
	constexpr auto maximum = 9007199254740991ULL;
	const auto tick = ecs.get<Clock>().tick;
	if (!state.next_generation || state.next_generation > ability_generation_limit || !state.next_activation ||
	    state.next_activation > maximum || state.grants.size() > ability_grant_limit ||
	    state.recovery.size() > ability_recovery_limit)
		throw std::runtime_error("Invalid saved ability roster bounds");
	if (state.work_tick > tick || state.work.size() > ability_grant_limit)
		throw std::runtime_error("Invalid saved ability work receipt bounds");
	std::set<std::uint64_t> work_activations;
	for (const auto &receipt : state.work)
	{
		const auto &catalogue = definition(ship).abilities.catalog->definitions;
		const auto found = catalogue.find(receipt.definition);
		if (found == catalogue.end() || found->second.family != receipt.family || !receipt.activation ||
		    receipt.activation >= state.next_activation || !work_activations.insert(receipt.activation).second ||
		    receipt.output <= 0 || receipt.output > 1000 || receipt.work != receipt.output ||
		    receipt.work_unit != "permille" || receipt.execution.empty() || receipt.execution.size() > 1024 ||
		    !receipt.generation || receipt.generation >= state.next_generation ||
		    !ability_source_exists(ship, receipt.source, receipt.owner))
			throw std::runtime_error("Invalid saved ability completed work");
	}
	const auto &abilities = definition(ship).abilities;
	const auto &definitions = abilities.catalog->definitions;
	std::set<std::uint64_t> generations, activations;
	std::set<std::string> exclusions, running_sources;
	for (const auto &[key, deadline] : state.recovery)
	{
		// Source and definition identifiers cannot contain ':'. Inspect actual
		// sparse ledger entries rather than manufacturing every possible key.
		const auto first = key.find(':'), second = first == std::string::npos ? first : key.find(':', first + 1);
		if (key.size() > 1024 || first == std::string::npos || second == std::string::npos || first == 0 ||
		    second == first + 1 || second + 1 == key.size() || key.find(':', second + 1) != std::string::npos)
			throw std::runtime_error("Invalid saved ability recovery identity");
		const auto source = key.substr(0, first), owner = key.substr(first + 1, second - first - 1),
		           reference = key.substr(second + 1);
		std::int64_t bound = 0;
		if (source == "apparatus")
		{
			const auto authored = abilities.apparatus_recovery_bounds.find(key);
			if (!ability_source_exists(ship, "equipment", owner) ||
			    authored == abilities.apparatus_recovery_bounds.end())
				throw std::runtime_error("Invalid saved apparatus recovery binding");
			bound = authored->second;
		}
		else
		{
			const auto ability = definitions.find(reference);
			if (!ability_source_exists(ship, source, owner) || ability == definitions.end())
				throw std::runtime_error("Invalid saved ability recovery source");
			bound = std::max(ability->second.recovery_ticks, ability->second.interruption_recovery_ticks);
		}
		if (!bound || deadline > maximum || deadline > tick + bound)
			throw std::runtime_error("Invalid saved ability recovery owner/deadline");
	}
	for (const auto &[id, grant] : state.grants)
	{
		const auto found = definitions.find(grant.definition);
		if (id != grant.id || id.empty() || id.size() > 1024 || found == definitions.end() ||
		    !ability_source_exists(ship, grant.source, grant.owner) || !grant.generation ||
		    grant.generation >= state.next_generation || !generations.insert(grant.generation).second ||
		    grant.activation >= state.next_activation || grant.request_id > maximum || grant.expiry < -1 ||
		    (grant.expiry >= 0 && (grant.expiry < static_cast<std::int64_t>(tick) ||
		                           grant.expiry > static_cast<std::int64_t>(tick) + 1000000)) ||
		    grant.parent.size() > 512 || grant.control.size() > 256 || grant.apparatus.size() > 256 ||
		    grant.aim_x < -fixed::unit || grant.aim_x > fixed::unit || grant.aim_y < -fixed::unit ||
		    grant.aim_y > fixed::unit)
			throw std::runtime_error("Invalid saved ability grant");
		if (grant.funding_permille < 0 || grant.funding_permille > 1000 || grant.credits.size() > 4096)
			throw std::runtime_error("Invalid saved ability payment");
		std::set<std::tuple<EntityId, std::string, std::string, std::string, std::string, std::string, std::int64_t>>
		    credit_keys;
		for (const auto &credit : grant.credits)
		{
			validate_saved_work_credit(Json(credit));
			if (!credit_keys
			         .emplace(credit.payer, credit.store, credit.resource, credit.property, credit.equipment,
			                  credit.officer, credit.compartment)
			         .second)
				throw std::runtime_error("Duplicate saved ability work credit");
		}
		const auto &ability = found->second;
		if (grant.retained_request && (!ability.sustained || grant.phase != "ready" || grant.control.empty()))
			throw std::runtime_error("Invalid retained ability request");

		if (grant.phase != "ready" && grant.phase != "preparing" && grant.phase != "active" && grant.phase != "paused")
			throw std::runtime_error("Invalid saved ability phase");
		const bool activated = grant.phase == "active" || (grant.phase == "paused" && grant.resume_phase == "active");
		if (grant.progress < 0 || grant.progress > ability.preparation_ticks * 1000 ||
		    (grant.phase == "paused" ? grant.resume_phase != "preparing" && grant.resume_phase != "active"
		                             : !grant.resume_phase.empty()) ||
		    (activated && (!ability.sustained || grant.progress != ability.preparation_ticks * 1000)))
			throw std::runtime_error("Contradictory saved ability preparation");
		if (grant.parent.empty())
		{
			const auto expected = initial.grants.find(id);
			if (expected == initial.grants.end() || !expected->second.parent.empty() ||
			    expected->second.definition != grant.definition || expected->second.source != grant.source ||
			    expected->second.owner != grant.owner || grant.expiry != -1 || !grant.grant_effect.empty())
				throw std::runtime_error("Saved static ability does not match its source grant");
		}
		else
		{
			const auto effect = ability_grant_templates.find(grant.grant_effect);
			if (effect == ability_grant_templates.end() || effect->second.ability != grant.definition ||
			    id != "temporary:" + grant.parent + ":" + grant.source + ":" + grant.owner ||
			    (effect->second.duration_ticks == 0
			         ? grant.expiry != -1
			         : grant.expiry < 0 ||
			               grant.expiry > static_cast<std::int64_t>(tick) + effect->second.duration_ticks))
				throw std::runtime_error("Invalid saved temporary ability lineage/lifetime");
		}
		const auto *control = resolve_ability_control(ship, grant);
		if (!grant.control.empty() && (!control || control->id != grant.control))
			throw std::runtime_error("Saved ability control binding is stale");
		if (grant.retained_request)
		{
			if (!control)
				throw std::runtime_error("Retained ability request has no control");
			const auto &controller = entities.at(ship).get<EntityControls>().controllers.at(control->controller);
			const auto &intent = controller.commands.at(control->enabled);
			if (controller.definition.commands.at(control->enabled).semantics != CommandSemantics::Request ||
			    !intent.value)
				throw std::runtime_error("Retained ability request disagrees with accepted intent");
		}
		if (grant.phase != "ready")
		{
			if (!running_sources.insert(ability_recovery_key(grant)).second)
				throw std::runtime_error("Duplicate saved running source ability");
			if ((ability.recovery_ticks || ability.interruption_recovery_ticks) &&
			    !state.recovery.contains(ability_recovery_key(grant)))
				throw std::runtime_error("Missing saved source recovery reservation");
			if (ability.apparatus_recovery_ticks &&
			    (grant.apparatus.empty() ||
			     !state.recovery.contains("apparatus:" + grant.apparatus + ":" + ability.apparatus_recovery_key)))
				throw std::runtime_error("Missing saved apparatus recovery reservation");
			if (!control || grant.control.empty() ||
			    (activated && (!grant.activation || !activations.insert(grant.activation).second)) ||
			    (ability.target == "self" && grant.target != ship) ||
			    (ability.target != "self" && (!grant.target || !entities.contains(grant.target))))
				throw std::runtime_error("Invalid saved active ability context");
			const auto apparatus =
			    control->apparatus.empty() && grant.source == "equipment" ? grant.owner : control->apparatus;
			if (grant.apparatus != apparatus)
				throw std::runtime_error("Saved ability apparatus binding is stale");
			const auto &intent = entities.at(ship)
			                         .get<EntityControls>()
			                         .controllers.at(control->controller)
			                         .commands.at(control->enabled);
			const auto semantics = entities.at(ship)
			                           .get<EntityControls>()
			                           .controllers.at(control->controller)
			                           .definition.commands.at(control->enabled)
			                           .semantics;
			if ((!ability.sustained && (!grant.request_id || intent.request_id != grant.request_id ||
			                            intent.value != 1 || !intent.accepted)) ||
			    (semantics != CommandSemantics::Request && grant.request_id) ||
			    (ability.sustained && grant.request_id && intent.request_id == grant.request_id &&
			     (intent.value != 1 || !intent.accepted)))
				throw std::runtime_error("Saved ability execution request is inconsistent");
			for (const auto &exclusion : ability.exclusions)
				if (!exclusions.insert(ability_exclusion_key(grant, exclusion)).second)
					throw std::runtime_error("Conflicting saved ability exclusions");
		}
	}
	for (const auto &[id, grant] : initial.grants)
		if (grant.parent.empty() && !state.grants.contains(id))
			throw std::runtime_error("Missing saved static ability grant");
}

void restore_abilities(const Json &saved, const std::set<EntityId> &departed_ships)
{
	(void)departed_ships; // Source-owned recovery remains on departed ships too.
	if (!saved.is_object() || saved.size() != 1 || !saved.contains("ships") || !saved.at("ships").is_array() ||
	    saved.at("ships").size() > 2048)
		throw std::runtime_error("Invalid ability continuation roster");
	std::set<EntityId> seen;
	for (const auto &row : saved.at("ships"))
	{
		if (!row.is_object() || row.size() != 2)
			throw std::runtime_error("Invalid ability continuation row");
		const EntityId ship = row.at("ship");
		if (!seen.insert(ship).second || !entities.contains(ship) || !entities.at(ship).has<AbilityRuntime>())
			throw std::runtime_error("Unknown or duplicate saved ability owner");
		auto state = row.at("state").get<AbilityRuntime>();
		if (Json(state) != row.at("state"))
			throw std::runtime_error("Invalid saved ability state fields");
		const auto initial = entities.at(ship).get<AbilityRuntime>();
		// Replacement restore runs on a disposable candidate World. Binding
		// resolution must see the complete restored grant roster, not the initial
		// static-only roster (temporary grants can change a dynamic seat choice).
		entities.at(ship).set<AbilityRuntime>(std::move(state));
		validate_ability_runtime(ship, entities.at(ship).get<AbilityRuntime>(), initial);
		for (const auto &control : definition(ship).abilities.controls)
		{
			const auto &commands = entities.at(ship).get<EntityControls>().controllers.at(control.controller).commands;
			for (const auto channel : {control.enabled, control.target})
				if (channel != ability_no_channel &&
				    !ability_generation_valid(ship, control.controller, channel, commands.at(channel).arguments))
					throw std::runtime_error("Saved ability command generation is stale");
		}
	}
	for (const auto &[ship, entity] : entities)
		if (entity.has<AbilityRuntime>() && !seen.contains(ship))
			throw std::runtime_error("Missing saved ability owner");
}
