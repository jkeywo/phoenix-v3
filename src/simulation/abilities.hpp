// Included inside World::Impl. Ability execution shares native grants and the
// prepared effect transaction; native effects retain their specialist state.
static bool ability_running(const AbilityGrantState &g)
{
	return g.phase == "preparing" || g.phase == "active" || g.phase == "paused";
}
static std::string ability_recovery_key(const AbilityGrantState &g)
{
	return g.source + ":" + g.owner + ":" + g.definition;
}
bool ability_source_exists(EntityId ship, const std::string &source, const std::string &owner) const
{
	if (!entities.contains(ship))
		return false;
	if (source == "equipment")
		return instance(entities.at(ship), owner).has_value();
	if (source != "officer")
		return false;
	const auto &officers = definition(ship).personnel.officers;
	return std::any_of(officers.begin(), officers.end(),
	                   [&](const auto &o)
	                   {
		                   return o.id == owner;
	                   });
}
const AbilityGrantState *ability_execution(EntityId ship, const std::string &grant) const
{
	if (!entities.contains(ship))
		return nullptr;
	const auto *state = entities.at(ship).try_get<AbilityRuntime>();
	if (!state)
		return nullptr;
	const auto at = state->grants.find(grant);
	return at == state->grants.end() ? nullptr : &at->second;
}
const AbilityDefinition &ability_definition(EntityId ship, const std::string &grant) const
{
	const auto *execution = ability_execution(ship, grant);
	if (!execution)
		throw std::runtime_error("Ability grant unavailable");
	return definition(ship).abilities.definitions.at(execution->definition);
}
std::uint64_t ability_recovery_until(EntityId ship, const AbilityGrantState &g) const
{
	const auto &ledger = entities.at(ship).get<AbilityRuntime>().recovery;
	const auto at = ledger.find(ability_recovery_key(g));
	auto until = at == ledger.end() ? 0 : at->second;
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	if (!d.apparatus_recovery_key.empty() && !g.apparatus.empty())
	{
		const auto shared = ledger.find("apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key);
		if (shared != ledger.end())
			until = std::max(until, shared->second);
	}
	return until;
}
const AbilityDefinition &ability_definition(EntityId ship, const AbilityGrantState &grant) const
{
	return definition(ship).abilities.definitions.at(grant.definition);
}
std::string ability_exclusion_key(const AbilityGrantState &g, const AbilityExclusionDefinition &e) const
{
	return e.scope + ":" + (e.scope == "apparatus" ? g.apparatus : g.source + ":" + g.owner) + ":" + e.id;
}
std::string ability_control_owner(EntityId ship, const AbilityControlDefinition &c) const
{
	if (c.source != "duty")
		return c.owner;
	const auto &d = definition(ship).personnel;
	const auto &state = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < d.officers.size(); ++i)
		if (state.officers[i].duty == c.owner && state.officers[i].present)
			return d.officers[i].id;
	return {};
}
const AbilityControlDefinition *resolve_ability_control(EntityId ship, const AbilityGrantState &g) const
{
	const AbilityControlDefinition *result = nullptr;
	for (const auto &c : definition(ship).abilities.controls)
	{
		if ((c.source == "equipment" ? "equipment" : "officer") != g.source ||
		    ability_control_owner(ship, c) != g.owner || (!c.ability.empty() && c.ability != g.definition))
			continue;
		if (c.ability.empty())
		{
			const auto choir = definition(ship).psychic.choirs.find(c.apparatus);
			if (choir == definition(ship).psychic.choirs.end() || !choir->second.powers.contains(g.definition))
				continue;
			// A legacy seat without an explicit selection is valid only for one
			// compatible available ability. Explicit endpoints handle multigrants.
			std::set<std::string> choices;
			choices.insert(g.definition);
			for (const auto &[key, candidate] : entities.at(ship).get<AbilityRuntime>().grants)
				if (candidate.source == g.source && candidate.owner == g.owner &&
				    choir->second.powers.contains(candidate.definition))
					choices.insert(candidate.definition);
			if (choices.size() != 1)
				continue;
		}
		if (result)
			return nullptr;
		result = &c;
	}
	return result;
}
bool ability_target_valid(EntityId ship, const AbilityControlDefinition &c, const AbilityDefinition &d, EntityId target,
                          ConsoleContacts &cache) const
{
	if (d.target == "self")
		return !target || target == ship;
	if (!target || !entities.contains(target) || departed(target) || combat_dead(entities.at(target)))
		return false;
	if (target == ship)
		return d.target == "ally";
	const auto &controller = entities.at(ship).get<EntityControls>().controllers.at(c.controller);
	const auto &contacts = console_contacts({ship, controller.definition.console}, cache);
	if (std::none_of(contacts.begin(), contacts.end(),
	                 [&](const auto &v)
	                 {
		                 return v.id == target;
	                 }))
		return false;
	return d.target != "ally" || factions.relationship(entities.at(ship).get<FactionMembership>().id,
	                                                   entities.at(target).get<FactionMembership>().id) == "friendly";
}
std::string ability_blocker(EntityId ship, const AbilityControlDefinition &c, const AbilityGrantState &g,
                            EntityId target, ConsoleContacts &cache) const
{
	if (!ability_source_exists(ship, g.source, g.owner) || departed(ship) || combat_dead(entities.at(ship)))
		return "Ability source unavailable";
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	if (g.source == "officer")
	{
		const auto &p = definition(ship).personnel;
		const auto &states = entities.at(ship).get<PersonnelState>();
		for (std::size_t i = 0; i < p.officers.size(); ++i)
			if (p.officers[i].id == g.owner)
			{
				const auto &o = states.officers[i];
				if ((d.require_healthy && !officer_healthy(o)) || (c.source == "duty" && !o.present))
					return "Officer unavailable";
			}
	}
	const bool continuing = g.phase == "active" || (g.phase == "paused" && g.resume_phase == "active");
	if ((!continuing && !ability_target_valid(ship, c, d, target, cache)) ||
	    (continuing && target != ship &&
	     (!entities.contains(target) || departed(target) || combat_dead(entities.at(target)))))
		return "Target unavailable";
	return {};
}
std::string ability_apparatus_blocker(EntityId ship, const AbilityGrantState &g) const
{
	const auto apparatus = g.apparatus.empty() && g.source == "equipment" ? g.owner : g.apparatus;
	if (apparatus.empty())
		return {};
	const auto fit = instance(entities.at(ship), apparatus);
	if (!fit || !fit->entity.get<Equipment>().enabled || structurally_disabled(entities.at(ship), apparatus) ||
	    boarding_inhibited(ship, apparatus))
		return "Apparatus unavailable";
	return {};
}
std::string ability_conflict(EntityId ship, const AbilityGrantState &g) const
{
	const auto &definitions = definition(ship).abilities.definitions;
	for (const auto &[key, other] : entities.at(ship).get<AbilityRuntime>().grants)
	{
		if (other.id == g.id || !ability_running(other))
			continue;
		// Duplicate grants of the same source ability are not another cast slot.
		if (ability_recovery_key(other) == ability_recovery_key(g))
			return "Ability already in use";
		for (const auto &wanted : definitions.at(g.definition).exclusions)
			for (const auto &held : definitions.at(other.definition).exclusions)
				if (ability_exclusion_key(g, wanted) == ability_exclusion_key(other, held))
					return "Exclusive ability is busy";
	}
	return {};
}
void initialise_abilities(flecs::entity ship)
{
	AbilityRuntime runtime;
	const auto add = [&](const std::string &source, const std::string &owner, const std::string &ability)
	{
		AbilityGrantState g;
		g.source = source;
		g.owner = owner;
		g.definition = ability;
		g.id = ability_recovery_key(g);
		g.generation = runtime.next_generation++;
		runtime.grants.emplace(g.id, std::move(g));
	};
	const auto id = ship.get<StableId>().id;
	for (const auto &o : definition(id).personnel.officers)
		for (const auto &ability : o.abilities)
			add("officer", o.id, ability);
	for (const auto &[owner, abilities] : definition(id).abilities.equipment_grants)
		for (const auto &ability : abilities)
			add("equipment", owner, ability);
	ship.set<AbilityRuntime>(std::move(runtime));
}
void ability_begin_recovery(EntityId ship, AbilityRuntime &runtime, const AbilityGrantState &g) const
{
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	const auto tick = ecs.get<Clock>().tick;
	if (d.recovery_ticks)
		runtime.recovery[ability_recovery_key(g)] =
		    std::max(runtime.recovery[ability_recovery_key(g)], tick + d.recovery_ticks);
	if (d.apparatus_recovery_ticks && !g.apparatus.empty())
	{
		const auto key = "apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key;
		runtime.recovery[key] = std::max(runtime.recovery[key], tick + d.apparatus_recovery_ticks);
	}
}
std::vector<std::string> ability_recovery_reservations(EntityId ship, const AbilityGrantState &g) const
{
	std::vector<std::string> keys;
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	if (d.recovery_ticks || d.interruption_recovery_ticks)
		keys.push_back(ability_recovery_key(g));
	if (d.apparatus_recovery_ticks && !g.apparatus.empty())
		keys.push_back("apparatus:" + g.apparatus + ":" + d.apparatus_recovery_key);
	return keys;
}
bool ability_recovery_capacity(EntityId ship, const AbilityGrantState &g) const
{
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	(void)effect_expiry(ecs.get<Clock>().tick,
	                    std::max({d.recovery_ticks, d.interruption_recovery_ticks, d.apparatus_recovery_ticks}));
	const auto &ledger = entities.at(ship).get<AbilityRuntime>().recovery;
	auto size = ledger.size();
	for (const auto &key : ability_recovery_reservations(ship, g))
		if (!ledger.contains(key))
			++size;
	return size <= ability_recovery_limit;
}
void stop_ability(EntityId ship, AbilityRuntime &runtime, AbilityGrantState &g)
{
	if (!ability_running(g))
		return;
	const auto &d = definition(ship).abilities.definitions.at(g.definition);
	const bool preparing = g.phase == "preparing" || g.resume_phase == "preparing";
	if (preparing && d.interruption_recovery_ticks)
	{
		const auto key = ability_recovery_key(g);
		runtime.recovery[key] = std::max(runtime.recovery[key], ecs.get<Clock>().tick + d.interruption_recovery_ticks);
	}
	else if (!preparing && d.recovery_on == "stop")
		ability_begin_recovery(ship, runtime, g);
	g.phase = "ready";
	g.resume_phase.clear();
	g.progress = 0;
	g.output_permille = 0;
}
void reset_ability_endpoint(EntityId ship, const AbilityControlDefinition &c)
{
	auto &enabled = entities.at(ship).get_mut<EntityControls>().controllers.at(c.controller);
	if (enabled.definition.commands.at(c.enabled).semantics == CommandSemantics::Request)
	{
		auto &command = enabled.commands.at(c.enabled);
		command.consumed = command.revision;
		command.accepted = false;
		command.arguments.clear();
		std::erase_if(controller_pending,
		              [&](const auto &pending)
		              {
			              return pending.address.entity == ship && pending.controller == c.controller &&
			                     pending.channel == c.enabled;
		              });
		if (c.target != ability_no_channel)
			clear_controller_settings(ship, c.controller, {c.target});
	}
	else if (c.target == ability_no_channel)
		clear_controller_settings(ship, c.controller, {c.enabled});
	else
		clear_controller_settings(ship, c.controller, {c.enabled, c.target});
	if (c.planner)
	{
		if (c.planning_target == ability_no_channel)
			clear_controller_settings(ship, *c.planner, {c.planning_enabled});
		else
			clear_controller_settings(ship, *c.planner, {c.planning_enabled, c.planning_target});
	}
}
void stop_abilities_for_duty(EntityId ship, const std::string &duty)
{
	auto *runtime = entities.at(ship).try_get_mut<AbilityRuntime>();
	if (!runtime)
		return;
	for (auto &[key, g] : runtime->grants)
		for (const auto &c : definition(ship).abilities.controls)
			if (c.id == g.control && c.source == "duty" && c.owner == duty)
			{
				stop_ability(ship, *runtime, g);
				g.control.clear();
				g.apparatus.clear();
				g.requested = false;
			}
	for (const auto &c : definition(ship).abilities.controls)
		if (c.source == "duty" && c.owner == duty)
			reset_ability_endpoint(ship, c);
	sync_psychic_contributions();
}
void clear_retired_ability_control(EntityId ship, const AbilityGrantState &removed, const AbilityRuntime &remaining)
{
	const auto *binding = resolve_ability_control(ship, removed);
	const auto control = removed.control.empty() && binding ? binding->id : removed.control;
	if (control.empty())
		return;
	for (const auto &[key, other] : remaining.grants)
		if (other.control == control && ability_running(other))
			return;
	for (const auto &c : definition(ship).abilities.controls)
		if (c.id == control)
			reset_ability_endpoint(ship, c);
}
void prepare_ability_grant(AbilityRuntime &runtime, EntityId ship, const ShieldReactionOperation &operation,
                           const std::string &parent)
{
	const auto source = operation.officer.empty() ? "equipment" : "officer";
	const auto owner = operation.officer.empty() ? operation.equipment : operation.officer;
	if (!ability_source_exists(ship, source, owner))
		throw std::runtime_error("Ability grant source unavailable");
	const auto id = "temporary:" + parent + ":" + source + ":" + owner;
	const auto old = runtime.grants.find(id);
	if (operation.kind == ShieldReactionKind::Remove)
	{
		if (old != runtime.grants.end())
		{
			stop_ability(ship, runtime, old->second);
			runtime.grants.erase(old);
		}
		return;
	}
	const auto &definition = ability_grant_templates.at(operation.effect);
	if (!this->definition(ship).abilities.definitions.contains(definition.ability))
		throw std::runtime_error("Ability grant definition unavailable");
	const auto expiry = effect_expiry(ecs.get<Clock>().tick, definition.duration_ticks);
	if (old != runtime.grants.end())
	{
		if (old->second.definition != definition.ability || old->second.grant_effect != operation.effect)
			throw std::runtime_error("Ability grant identity changed definition");
		old->second.expiry = refresh_effect_expiry(old->second.expiry, expiry);
		return;
	}
	if (runtime.grants.size() >= ability_grant_limit || runtime.next_generation >= ability_generation_limit)
		throw std::runtime_error("Ability grant capacity exhausted");
	AbilityGrantState g;
	g.id = id;
	g.source = source;
	g.owner = owner;
	g.parent = parent;
	g.grant_effect = operation.effect;
	g.definition = definition.ability;
	g.expiry = expiry;
	g.generation = runtime.next_generation++;
	runtime.grants.emplace(id, std::move(g));
}
std::string ability_apply_group(EntityId ship, const AbilityGrantState &g,
                                const std::optional<ShieldReactionDefinition> &group)
{
	if (!group)
		return {};
	return execute_effect_group(*group, ship, g.target ? g.target : ship,
	                            "ability." + std::to_string(ship) + "." + ability_recovery_key(g));
}
void ability_request_result(EntityId ship, const AbilityGrantState &g, bool accepted)
{
	if (!g.request_id)
		return;
	for (const auto &control : definition(ship).abilities.controls)
		if (control.id == g.control)
		{
			auto &command = entities.at(ship)
			                    .get_mut<EntityControls>()
			                    .controllers.at(control.controller)
			                    .commands.at(control.enabled);
			if (command.request_id == g.request_id)
				command.accepted = accepted;
		}
}
void activate_ability(EntityId ship, const std::string &key, bool activation_committed = false)
{
	const auto *before = ability_execution(ship, key);
	if (!before)
		return;
	const auto snapshot = *before;
	const auto &d = definition(ship).abilities.definitions.at(snapshot.definition);
	ConsoleContacts contacts;
	const auto *control = resolve_ability_control(ship, snapshot);
	if (!control || !ability_blocker(ship, *control, snapshot, snapshot.target, contacts).empty() ||
	    !ability_apparatus_blocker(ship, snapshot).empty())
	{
		auto &state = entities.at(ship).get_mut<AbilityRuntime>();
		stop_ability(ship, state, state.grants.at(key));
		ability_request_result(ship, snapshot, false);
		return;
	}
	if (entities.at(ship).get<AbilityRuntime>().next_activation >= 9007199254740991ULL)
		throw std::runtime_error("Ability activation identity exhausted");
	const auto failure =
	    activation_committed ? std::string{} : ability_apply_group(ship, snapshot, d.activation_effect);
	auto &runtime = entities.at(ship).get_mut<AbilityRuntime>();
	const auto found = runtime.grants.find(key);
	if (found == runtime.grants.end() || found->second.generation != snapshot.generation)
		return;
	auto &g = found->second;
	if (!failure.empty())
	{
		if (d.preparation_ticks)
			stop_ability(ship, runtime, g);
		else
			g.phase = "ready";
		g.blocker = failure;
		ability_request_result(ship, snapshot, false);
		return;
	}
	g.activation = runtime.next_activation++;
	g.phase = d.sustained ? "active" : "ready";
	g.progress = d.preparation_ticks * 1000;
	if (d.recovery_on == "activation" || !d.sustained)
		ability_begin_recovery(ship, runtime, g);
}
void prepare_abilities(EntityId ship)
{
	auto &runtime = entities.at(ship).get_mut<AbilityRuntime>();
	const auto tick = ecs.get<Clock>().tick;
	std::set<std::string> held_recovery;
	for (const auto &[key, grant] : runtime.grants)
		if (ability_running(grant))
			for (const auto &recovery : ability_recovery_reservations(ship, grant))
				held_recovery.insert(recovery);
	std::erase_if(runtime.recovery,
	              [&](const auto &entry)
	              {
		              return entry.second <= tick && !held_recovery.contains(entry.first);
	              });
	ConsoleContacts cache;
	std::vector<std::string> keys;
	for (const auto &[key, g] : runtime.grants)
		keys.push_back(key);
	for (const auto &key : keys)
	{
		auto *current = entities.at(ship).get_mut<AbilityRuntime>().grants.contains(key)
		                    ? &entities.at(ship).get_mut<AbilityRuntime>().grants.at(key)
		                    : nullptr;
		if (!current)
			continue;
		auto &g = *current;
		g.output_permille = 0;
		g.funding_blocked = false;
		g.blocker.clear();
		if (g.expiry >= 0 && static_cast<std::uint64_t>(g.expiry) <= tick)
		{
			stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
			const auto removed = g;
			entities.at(ship).get_mut<AbilityRuntime>().grants.erase(key);
			clear_retired_ability_control(ship, removed, entities.at(ship).get<AbilityRuntime>());
			continue;
		}
		const auto *c = resolve_ability_control(ship, g);
		if (!c)
		{
			stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
			g.control.clear();
			g.requested = false;
			g.blocker = "No compatible ability control";
			continue;
		}
		const auto &d = definition(ship).abilities.definitions.at(g.definition);
		if (const auto *selected = ability_control_grant(ship, *c); selected && selected->id != g.id)
		{
			g.requested = false;
			continue;
		}
		auto &controller = entities.at(ship).get_mut<EntityControls>().controllers.at(c->controller);
		auto &command = controller.commands.at(c->enabled);
		const bool discrete = controller.definition.commands.at(c->enabled).semantics == CommandSemantics::Request;
		const bool new_request = discrete && command.request_id && command.consumed < command.revision;
		const bool start_request = new_request && command.value != 0;
		if (new_request)
		{
			command.consumed = command.revision;
			if (!command.arguments.empty() &&
			    (command.arguments.size() != 1 || command.arguments.front() != static_cast<std::int64_t>(g.generation)))
			{
				command.accepted = false;
				continue;
			}
			command.accepted = true;
			if (!command.value)
			{
				stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
				g.requested = false;
				continue;
			}
			if (ability_running(g))
			{
				command.accepted = false;
				continue;
			}
		}
		const auto target = d.target == "self" ? ship
		                    : c->target == ability_no_channel
		                        ? 0
		                        : static_cast<EntityId>(controller.commands.at(c->target).value);
		const bool requested = discrete ? ability_running(g) || start_request : command.value != 0;
		if (ability_running(g) && (g.control != c->id || g.target != target || !requested))
			stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
		g.control = c->id;
		g.apparatus = c->apparatus.empty() && g.source == "equipment" ? g.owner : c->apparatus;
		g.requested = requested;
		if (!requested)
		{
			continue;
		}
		g.blocker = ability_blocker(ship, *c, g, target, cache);
		if (!g.blocker.empty())
		{
			stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
			if (new_request)
				command.accepted = false;
			continue;
		}
		if (target != ship)
			for (const auto &contact : console_contacts({ship, controller.definition.console}, cache))
				if (contact.id == target)
				{
					g.aim_x = contact.x;
					g.aim_y = contact.y;
				}
		const auto apparatus_blocker = ability_apparatus_blocker(ship, g);
		if (!apparatus_blocker.empty())
		{
			g.blocker = apparatus_blocker;
			if (new_request)
				command.accepted = false;
			if (ability_running(g))
			{
				if ((g.phase == "preparing" && d.interruption == "cancel") || d.shortage == "stop")
					stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
				else if (g.phase != "paused")
				{
					g.resume_phase = g.phase;
					g.phase = "paused";
				}
			}
			continue;
		}
		if (g.phase == "paused")
		{
			g.phase = g.resume_phase;
			g.resume_phase.clear();
		}
		if (ability_running(g) || (discrete && !start_request))
			continue;
		if (ability_recovery_until(ship, g) > tick)
		{
			g.blocker = "Ability recovering";
			if (new_request)
				command.accepted = false;
			continue;
		}
		g.blocker = ability_conflict(ship, g);
		if (!g.blocker.empty())
		{
			// Admission may see several starts before their physical phase. The
			// stable loser is rejected, not left queued to start after the winner.
			if (discrete)
				command.accepted = false;
			else
				clear_controller_settings(ship, c->controller, {c->enabled});
			g.requested = false;
			continue;
		}
		g.target = target;
		g.request_id = discrete ? command.request_id : 0;
		if (!ability_recovery_capacity(ship, g))
		{
			g.blocker = "Ability recovery capacity exhausted";
			if (discrete)
				command.accepted = false;
			else
				clear_controller_settings(ship, c->controller, {c->enabled});
			continue;
		}
		const auto snapshot = g;
		auto entry = d.start_effect;
		if (!d.preparation_ticks && d.activation_effect)
		{
			if (!entry)
				entry = d.activation_effect;
			else
			{
				if (d.activation_effect->energy)
					entry->payer = d.activation_effect->payer;
				entry->energy += d.activation_effect->energy;
				entry->operations.insert(entry->operations.end(), d.activation_effect->operations.begin(),
				                         d.activation_effect->operations.end());
			}
		}
		if (entities.at(ship).get<AbilityRuntime>().next_activation >= 9007199254740991ULL)
			throw std::runtime_error("Ability activation identity exhausted");
		const auto failure = ability_apply_group(ship, snapshot, entry);
		auto &updated = entities.at(ship).get_mut<AbilityRuntime>();
		if (!updated.grants.contains(key) || updated.grants.at(key).generation != snapshot.generation)
			continue;
		auto &started = updated.grants.at(key);
		if (!failure.empty())
		{
			started.blocker = failure;
			if (discrete)
				command.accepted = false;
			continue;
		}
		started.phase = "preparing";
		started.progress = 0;
		for (const auto &recovery : ability_recovery_reservations(ship, started))
			updated.recovery.try_emplace(recovery, 0);
		if (!d.preparation_ticks)
			activate_ability(ship, key, true);
	}
}
void request_ability_resources(EntityId ship)
{
	auto *power = entities.at(ship).try_get_mut<ShipResources>();
	if (!power)
		return;
	// Native choirs have no separate baseline energy use; their active ability
	// requests replace the historical aggregate seat demand on the same slot.
	for (const auto &[id, choir] : definition(ship).psychic.choirs)
		if (const auto fit = instance(entities.at(ship), id); fit && fit->entity.has<ResourceBinding>())
			request_system_resource(*power, fit->entity.get<ResourceBinding>().slot, 0, false, 0);
	std::map<std::string, std::int64_t> demand;
	for (const auto &[key, g] : entities.at(ship).get<AbilityRuntime>().grants)
		if (ability_running(g) && ability_apparatus_blocker(ship, g).empty())
		{
			const auto &d = definition(ship).abilities.definitions.at(g.definition);
			const auto preparation = g.phase == "preparing" || g.resume_phase == "preparing";
			const auto amount = preparation ? d.preparation_energy_per_second : d.energy_per_second;
			if (amount && !g.apparatus.empty())
			{
				if (demand[g.apparatus] > resource_limit - amount)
					throw std::runtime_error("Ability resource demand overflow");
				demand[g.apparatus] += amount;
			}
		}
	for (const auto &[apparatus, amount] : demand)
	{
		const auto fit = instance(entities.at(ship), apparatus);
		if (!fit || !fit->entity.has<ResourceBinding>())
			continue;
		const auto slot = fit->entity.get<ResourceBinding>().slot;
		const auto old = power->requests.at(slot);
		request_system_resource(*power, slot, old.duty_permille, old.discrete, old.extra_rate + amount,
		                        old.discrete_amount);
	}
}
bool reject_unfunded_abilities(flecs::entity ship, const ShipResourceAllocation &allocation)
{
	bool removed = false;
	const auto id = ship.get<StableId>().id;
	auto &power = ship.get_mut<ShipResources>();
	for (auto &[key, g] : ship.get_mut<AbilityRuntime>().grants)
	{
		if (!ability_running(g) || g.funding_blocked || g.apparatus.empty() ||
		    !ability_apparatus_blocker(id, g).empty())
			continue;
		const auto &d = definition(id).abilities.definitions.at(g.definition);
		const auto amount = g.phase == "preparing" || g.resume_phase == "preparing" ? d.preparation_energy_per_second
		                                                                            : d.energy_per_second;
		if (!amount || d.shortage == "proportional")
			continue;
		const auto fit = instance(ship, g.apparatus);
		if (!fit || !fit->entity.has<ResourceBinding>())
			continue;
		const auto slot = fit->entity.get<ResourceBinding>().slot;
		const auto &grant = allocation.grants().at(slot);
		if (grant.blocked || grant.supply_permille < 1000)
		{
			auto &request = power.requests.at(slot);
			if (request.extra_rate < amount)
				throw std::runtime_error("Ability operating request lost its owner");
			request.extra_rate -= amount;
			g.funding_blocked = true;
			removed = true;
		}
	}
	return removed;
}
void refresh_ability_work(bool advance = true)
{
	for (const auto &[ship, entity] : entities)
	{
		auto *runtime = entity.try_get_mut<AbilityRuntime>();
		if (!runtime)
			continue;
		std::vector<std::string> completed;
		for (auto &[key, g] : runtime->grants)
		{
			g.output_permille = 0;
			g.funding_permille = 0;
			g.fault_permille = 0;
			if (!ability_running(g) || !ability_apparatus_blocker(ship, g).empty())
				continue;
			const auto &d = definition(ship).abilities.definitions.at(g.definition);
			const auto preparing = g.phase == "preparing" || g.resume_phase == "preparing";
			const auto demand = preparing ? d.preparation_energy_per_second : d.energy_per_second;
			std::int64_t funding = demand ? 0 : 1000;
			std::int64_t faults = 1000;
			if (!g.apparatus.empty())
			{
				const auto fit = instance(entity, g.apparatus);
				if (fit && fit->entity.has<ResourceBinding>())
					if (const auto *power = entity.try_get<ShipResources>())
						funding = power->grants.at(fit->entity.get<ResourceBinding>().slot).supply_permille;
				faults = fault_effectiveness(entity, g.apparatus);
			}
			if ((g.funding_blocked || funding < 1000) && d.shortage != "proportional")
			{
				if (advance && d.shortage == "stop")
					stop_ability(ship, *runtime, g);
				else if (advance && g.phase != "paused")
				{
					g.resume_phase = g.phase;
					g.phase = "paused";
				}
				g.blocker = "Insufficient ability funding";
				continue;
			}
			if (!advance && g.phase == "paused")
				continue;
			if (advance && g.phase == "paused")
			{
				g.phase = g.resume_phase;
				g.resume_phase.clear();
			}
			if (preparing)
			{
				if (advance)
				{
					g.progress = std::min(d.preparation_ticks * 1000, g.progress + funding);
					if (g.progress == d.preparation_ticks * 1000)
						completed.push_back(key);
				}
			}
			else
			{
				g.funding_permille = funding;
				g.fault_permille = faults;
				g.output_permille = funding * faults / 1000;
			}
		}
		for (const auto &key : completed)
		{
			auto *g = ability_execution(ship, key);
			if (!g)
				continue;
			ConsoleContacts cache;
			const auto *c = resolve_ability_control(ship, *g);
			if (c && ability_blocker(ship, *c, *g, g->target, cache).empty())
				activate_ability(ship, key);
			else
				stop_ability(ship, entity.get_mut<AbilityRuntime>(), entity.get_mut<AbilityRuntime>().grants.at(key));
		}
	}
}
