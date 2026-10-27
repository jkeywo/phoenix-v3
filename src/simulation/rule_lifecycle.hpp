// Included inside World::Impl. Derived addresses never retain Flecs pointers;
// ordinary bodies and native active records own the persisted clocks.
struct RuleClockAddress
{
	EntityId ship = 0;
	std::uint64_t active = 0;
};
std::map<std::string, RuleClockAddress> rule_clock_addresses;

std::vector<NativeRuleSource> lifecycle_rule_sources() const
{
	std::vector<NativeRuleSource> sources;
	for (const auto &[id, ship] : entities)
	{
		auto local = rule_sources(ship);
		sources.insert(sources.end(), std::make_move_iterator(local.begin()), std::make_move_iterator(local.end()));
	}
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.context.ordnance)
		{
			auto source = active_native_rule_source(effect);
			if (source.bindings && !source.bindings->empty())
				sources.push_back(std::move(source));
		}
	}
	std::sort(sources.begin(), sources.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tie(a.context.subject.entity, a.context.subject.ordnance, a.key) <
		                 std::tie(b.context.subject.entity, b.context.subject.ordnance, b.key);
	          });
	return sources;
}
void rebuild_rule_clock_addresses()
{
	rule_clock_capacity_cache.reset();
	rule_clock_addresses.clear();
	for (const auto &[id, ship] : entities)
		if (const auto *runtime = ship.try_get<RuleRuntime>())
			for (const auto &[key, state] : runtime->executions)
				rule_clock_addresses.emplace(key, RuleClockAddress{id, 0});
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		for (const auto &[key, state] : effect.clocks)
			rule_clock_addresses.emplace(key, RuleClockAddress{0, effect.id});
	}
	if (rule_clock_addresses.size() > rule_instance_limit)
		throw std::runtime_error("rules: execution capacity exhausted");
}
RuleExecutionState *find_rule_execution(const std::string &key)
{
	const auto found = rule_clock_addresses.find(key);
	if (found == rule_clock_addresses.end())
		return nullptr;
	if (found->second.active)
	{
		const auto entity = active_rule_entity(found->second.active);
		if (!entity)
			return nullptr;
		auto &clocks = entity.get_mut<ActiveRuleEffect>().clocks;
		const auto state = clocks.find(key);
		return state == clocks.end() ? nullptr : &state->second;
	}
	const auto owner = entities.find(found->second.ship);
	if (owner == entities.end())
		return nullptr;
	auto *runtime = owner->second.try_get_mut<RuleRuntime>();
	if (!runtime)
		return nullptr;
	const auto state = runtime->executions.find(key);
	return state == runtime->executions.end() ? nullptr : &state->second;
}
const RuleExecutionState *readonly_rule_execution(const std::string &key) const
{
	const auto found = rule_clock_addresses.find(key);
	if (found == rule_clock_addresses.end())
		return nullptr;
	if (found->second.active)
	{
		const auto entity = active_rule_entity(found->second.active);
		if (!entity)
			return nullptr;
		const auto &clocks = entity.get<ActiveRuleEffect>().clocks;
		const auto state = clocks.find(key);
		return state == clocks.end() ? nullptr : &state->second;
	}
	const auto owner = entities.find(found->second.ship);
	if (owner == entities.end())
		return nullptr;
	const auto *runtime = owner->second.try_get<RuleRuntime>();
	if (!runtime)
		return nullptr;
	const auto state = runtime->executions.find(key);
	return state == runtime->executions.end() ? nullptr : &state->second;
}
std::string parent_rule_execution_key(const std::string &source) const
{
	if (!source.starts_with("rule."))
		return {};
	const auto separator = source.find('.', 5);
	return separator == std::string::npos ? source : source.substr(0, separator);
}
bool rule_grant_available(EntityId, const std::string &source, RuleSourceAvailabilityCache *scratch = nullptr) const
{
	const auto key = parent_rule_execution_key(source);
	const auto *state = readonly_rule_execution(key);
	if (!state || !rule_templates.contains(state->definition) ||
	    rule_templates.at(state->definition).phase != RulePhase::Maintained)
		return true;
	if (!state->eligible || !state->active || state->stopped || !state->output)
		return false;
	const auto address = rule_clock_addresses.find(key);
	if (address == rule_clock_addresses.end())
		return false;
	if (address->second.active)
	{
		const auto entity = active_rule_entity(address->second.active);
		return entity && entity.get<ActiveRuleEffect>().expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick) &&
		       active_rule_target_available(entity.get<ActiveRuleEffect>());
	}
	const auto host = entities.find(address->second.ship);
	if (host == entities.end())
		return false;
	RuleSourceAvailabilityCache local;
	auto &cache = scratch ? *scratch : local;
	auto found = cache.find(address->second.ship);
	if (found == cache.end())
	{
		std::map<std::string, bool> availability;
		for (const auto &view : rule_sources(host->second))
			availability.emplace(view.key, view.eligible);
		found = cache.emplace(address->second.ship, std::move(availability)).first;
	}
	const auto source_state = found->second.find(state->source_key);
	return source_state != found->second.end() && source_state->second;
}
bool rule_lease_available(const std::string &source) const
{
	const auto *state = readonly_rule_execution(parent_rule_execution_key(source));
	if (!state || !rule_templates.contains(state->definition) ||
	    rule_templates.at(state->definition).phase != RulePhase::Maintained)
		return true;
	if (!state->eligible || !state->active || state->stopped)
		return false;
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	if (rule_templates.at(state->definition).funding && state->last != now)
		return state->lease_tick == now && state->lease_permille > 0 &&
		       (state->stop_tick < 0 || state->stop_tick > now);
	return state->output > 0;
}
std::int64_t rule_source_output(EntityId ship, const std::string &source) const
{
	const auto *state = readonly_rule_execution(parent_rule_execution_key(source));
	if (!state || !rule_templates.contains(state->definition) ||
	    rule_templates.at(state->definition).phase != RulePhase::Maintained)
		return 1000;
	return rule_grant_available(ship, source) ? state->output : 0;
}
bool trait_rule_operation_matches(const EffectOperation &operation, const NativeRuleContext &context) const
{
	if (context.property_role.empty() && context.property_filter.empty())
		return true;
	if (operation.kind != EffectOperationKind::Apply || !modifier_templates.contains(operation.effect))
		return true;
	const auto role = operation.role == "$role" ? context.property_role : operation.role;
	for (const auto &contribution : modifier_templates.at(operation.effect).contributions)
		if ((role.empty() || contribution.role == role) &&
		    (context.property_filter.empty() || contribution.property == context.property_filter))
			return true;
	return false;
}
RuleEvaluation evaluate_source_rule(const RuleDefinition &rule, const NativeRuleContext &context) const
{
	if ((!context.property_role.empty() || !context.property_filter.empty()) &&
	    std::none_of(rule.effects.operations.begin(), rule.effects.operations.end(),
	                 [&](const auto &op)
	                 {
		                 return trait_rule_operation_matches(op, context);
	                 }))
		return {RuleEvaluationStatus::Unmatched, {}};
	auto result = evaluate_rule(rule, freeze_rule_context(rule, context));
	if (result.status == RuleEvaluationStatus::Ready)
		std::erase_if(result.quote.effects.operations,
		              [&](const auto &op)
		              {
			              return !trait_rule_operation_matches(op, context);
		              });
	return result;
}
NativeRuleContext rule_applied_context(const NativeRuleContext &context) const
{
	NativeRuleContext applied;
	applied.owner = context.owner;
	applied.source = context.source;
	applied.instigator = context.instigator;
	applied.subject = context.subject;
	applied.target = context.target;
	applied.hit = context.hit;
	applied.property_role = context.property_role;
	applied.property_filter = context.property_filter;
	applied.region = context.region;
	applied.owner_grant = context.owner_grant;
	applied.owner_effect_kind = context.owner_effect_kind;
	applied.owner_effect_id = context.owner_effect_id;
	applied.output = 0;
	return applied;
}
std::string rule_application_equipment(const NativeRuleContext &context, const std::string &binding) const
{
	if (binding.empty() || binding.front() != '$')
		return binding;
	if (binding == "$hit")
		return context.hit.kind == PayloadOutcomeKind::Shield
		           ? "effect:" + std::to_string(context.hit.contribution) + ":" + context.hit.contribution_name
		           : context.hit.equipment;
	const auto actor = rule_actor(context, binding.substr(1));
	return actor.kind == PayloadOutcomeKind::Shield
	           ? "effect:" + std::to_string(actor.contribution) + ":" + actor.contribution_name
	           : actor.equipment;
}
std::string rule_operation_equipment(const NativeRuleContext &context, const EffectOperation &operation) const
{
	if (modifier_templates.contains(operation.effect))
	{
		const auto &definition = modifier_templates.at(operation.effect);
		const bool custom = std::any_of(definition.contributions.begin(), definition.contributions.end(),
		                                [&](const auto &row)
		                                {
			                                return property_catalogue->contains(row.property);
		                                });
		if (custom)
		{
			auto actor = rule_actor(context, operation.target);
			if (!operation.officer.empty())
			{
				actor.equipment.clear();
				actor.officer = operation.officer;
				actor.compartment = -1;
			}
			if (operation.equipment.empty())
				return custom_modifier_target(actor);
			if (operation.equipment.front() == '$' && operation.equipment != "$hit")
				return custom_modifier_target(rule_actor(context, operation.equipment.substr(1)));
		}
	}
	return rule_application_equipment(context, operation.equipment);
}
void capture_rule_custom_targets(const RuleDefinition &rule, const NativeRuleContext &context)
{
	for (const auto &operation : rule.effects.operations)
		if (operation.kind == EffectOperationKind::Apply && modifier_templates.contains(operation.effect))
		{
			const auto actor = rule_actor(context, operation.target);
			capture_custom_property_owner(custom_property_address(
			    custom_property_actor(actor.entity, rule_operation_equipment(context, operation))));
		}
}
bool same_rule_application(const RuleDefinition &rule, const NativeRuleContext &previous,
                           const NativeRuleContext &current) const
{
	for (const auto &operation : rule.effects.operations)
		if (operation.kind == EffectOperationKind::Apply)
		{
			auto before = rule_actor(previous, operation.target), after = rule_actor(current, operation.target);
			if (before.kind == PayloadOutcomeKind::Entity)
			{
				before.equipment.clear();
				before.officer.clear();
			}
			if (after.kind == PayloadOutcomeKind::Entity)
			{
				after.equipment.clear();
				after.officer.clear();
			}
			if (before != after ||
			    rule_operation_equipment(previous, operation) != rule_operation_equipment(current, operation))
				return false;
			if (modifier_templates.contains(operation.effect) &&
			    (previous.property_filter != current.property_filter ||
			     (operation.role == "$role" && previous.property_role != current.property_role)))
				return false;
		}
	return true;
}
bool maintained_modifier_exists(const RuleExecutionState &state, const EffectOperation &operation) const
{
	if (operation.kind != EffectOperationKind::Apply || !modifier_templates.contains(operation.effect))
		return false;
	const auto actor = rule_actor(state.context, operation.target);
	const auto owner = entities.find(actor.entity);
	if (actor.ordnance || owner == entities.end())
		return false;
	const auto *properties = owner->second.try_get<PropertyEffectOwner>();
	if (!properties)
		return false;
	const auto equipment = rule_operation_equipment(state.context, operation);
	const auto at = properties->grants.find({state.key + "." + operation.grant, equipment});
	return at != properties->grants.end() && at->second.get<ActiveEffectIdentity>().definition == operation.effect;
}
void withdraw_rule_grants(RuleExecutionState &state)
{
	const auto definition = rule_templates.find(state.definition);
	if (definition == rule_templates.end() || definition->second.phase != RulePhase::Maintained)
	{
		state.active = false;
		state.output = 0;
		state.applied_context = {};
		return;
	}
	if (!state.active)
	{
		state.applied_context = {};
		state.output = 0;
		return;
	}
	bool changed = false, abilities_changed = false;
	for (const auto &operation : definition->second.effects.operations)
		if (operation.kind == EffectOperationKind::Apply)
		{
			const auto actor = rule_actor(state.applied_context, operation.target);
			auto applied_actor = actor;
			if (applied_actor.kind != PayloadOutcomeKind::System)
				applied_actor.equipment.clear();
			applied_actor.officer.clear();
			const auto source = state.key + "." + operation.grant;
			const auto equipment = rule_operation_equipment(state.applied_context, operation);
			if (const auto owner = entities.find(actor.entity); owner != entities.end() && !actor.ordnance)
			{
				if (auto *shields = owner->second.try_get_mut<ShieldComponent>();
				    shields && shield_templates.contains(operation.effect))
					changed = std::erase_if(shields->contributions,
					                        [&](const auto &c)
					                        {
						                        return c.source == source && c.definition_id == operation.effect &&
						                               c.equipment == equipment;
					                        }) > 0 ||
					          changed;
				if (const auto *properties = owner->second.try_get<PropertyEffectOwner>();
				    properties && modifier_templates.contains(operation.effect))
				{
					const auto at = properties->grants.find({source, equipment});
					if (at != properties->grants.end() &&
					    at->second.get<ActiveEffectIdentity>().definition == operation.effect)
					{
						erase_modifier(at->second);
						changed = true;
					}
				}
				if (auto *abilities = owner->second.try_get_mut<AbilityRuntime>();
				    abilities && ability_grant_templates.contains(operation.effect))
				{
					std::vector<std::string> retired;
					for (const auto &[key, grant] : abilities->grants)
						if (grant.parent == source && grant.grant_effect == operation.effect &&
						    grant.source == (operation.officer.empty() ? "equipment" : "officer") &&
						    grant.owner == (operation.officer.empty() ? equipment : operation.officer))
							retired.push_back(key);
					for (const auto &key : retired)
					{
						stop_ability(actor.entity, *abilities, abilities->grants.at(key));
						const auto removed = abilities->grants.at(key);
						abilities->grants.erase(key);
						clear_retired_ability_control(actor.entity, removed, *abilities);
						changed = abilities_changed = true;
					}
				}

				if (process_grant_templates.contains(operation.effect))
				{
					EffectGroupInvocation frame;
					frame.rule_context = state.applied_context;
					frame.hit_context = state.applied_context.hit;
					const auto address = custom_property_address(effect_property_actor(
					    frame, actor, operation.equipment, operation.officer, operation.compartment));
					if (const auto *runtime = process_runtime(address))
					{
						std::vector<std::pair<std::string, std::uint64_t>> retired;
						for (const auto &[id, process] : runtime->processes)
							if (process.source == source && process.grant_effect == operation.effect)
								retired.emplace_back(id, process.generation);
						for (const auto &[id, generation] : retired)
						{
							(void)stop_process(address, id, true);
							if (auto *remaining = process_runtime(address))
								if (const auto at = remaining->processes.find(id);
								    at != remaining->processes.end() && at->second.generation == generation)
									remaining->processes.erase(at);
							changed = true;
						}
					}
				}
			}
			changed = erase_active_rule_effects_if(
			              [&](const auto &effect)
			              {
				              return periodic_templates.contains(operation.effect) &&
				                     effect.definition == operation.effect &&
				                     same_payload_context(effect.context, rule_hit_context(applied_actor)) &&
				                     effect.source == source;
			              }) > 0 ||
			          changed;
		}
	if (abilities_changed)
		sync_psychic_contributions();
	if (changed)
	{
		if (effect_commit_epoch == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Effect commit epoch exhausted");
		++effect_commit_epoch;
	}
	state.active = false;
	state.output = 0;
	state.applied_context = {};
}
void retire_rule_state(RuleExecutionState &state, bool deactivate)
{
	if (deactivate && state.eligible && rule_templates.contains(state.definition) &&
	    rule_templates.at(state.definition).trigger == RuleTrigger::Deactivate)
	{
		auto context = state.context;
		context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
		enqueue_rule(state.definition, state.key, context, RuleEventPhase::Source, context.tick);
	}
	withdraw_rule_grants(state);
	state.eligible = false;
	// A false condition must not erase the reason a later true edge needs a
	// fresh stop rearm. Mature a pending stop even while the rule is unmatched.
	if (state.stop_tick >= 0 && state.stop_tick <= static_cast<std::int64_t>(ecs.get<Clock>().tick))
	{
		state.stopped = true;
		state.stop_tick = -1;
	}
	state.lease_tick = -1;
	state.lease_permille = 0;
	state.credits.clear();
}
RuleExecutionState *ensure_rule_execution(const NativeRuleSource &source, const RuleBinding &binding,
                                          const NativeRuleContext &context, bool create)
{
	const auto key = rule_key(source.key, binding.id, context.subject);
	if (auto *existing = find_rule_execution(key))
		return existing;
	if (!create)
		return nullptr;
	if (rule_clock_addresses.size() >= rule_instance_limit)
		throw std::runtime_error("rules: execution capacity exhausted");
	std::map<std::string, RuleExecutionState> *clocks = nullptr;
	std::uint64_t generation = 1;
	std::int64_t application = context.tick;
	RuleClockAddress address;
	if (source.active_effect)
	{
		if (const auto entity = active_rule_entity(source.active_effect))
		{
			auto &effect = entity.get_mut<ActiveRuleEffect>();
			clocks = &effect.clocks;
			address.active = effect.id;
			if (periodic_templates.contains(effect.definition))
				application = effect.expiry - periodic_templates.at(effect.definition).duration_ticks;
		}
	}
	else
	{
		address.ship = source.owner.entity ? source.owner.entity : source.context.subject.entity;
		const auto host = entities.find(address.ship);
		if (host == entities.end())
			return nullptr;
		if (!host->second.has<RuleRuntime>())
			host->second.set<RuleRuntime>({});
		auto &runtime = host->second.get_mut<RuleRuntime>();
		if (runtime.next_generation >= 9007199254740991ULL)
			throw std::runtime_error("rules: execution generation exhausted");
		generation = runtime.next_generation++;
		clocks = &runtime.executions;
	}
	if (!clocks)
		return nullptr;
	RuleExecutionState state;
	state.key = key;
	state.definition = binding.rule;
	state.binding = binding.id;
	state.source_key = source.key;
	state.owner_kind = source.kind;
	state.context = context;
	state.generation = generation;
	state.started = application;
	const auto &rule = rule_templates.at(binding.rule);
	state.next = application + rule.period_ticks;
	auto &result = clocks->emplace(key, std::move(state)).first->second;
	rule_clock_addresses.emplace(key, address);
	rule_clock_capacity_cache.reset();
	return &result;
}
void erase_rule_execution(const std::string &key)
{
	erase_custom_property_rate_source(key);
	const auto found = rule_clock_addresses.find(key);
	if (found == rule_clock_addresses.end())
		return;
	if (found->second.active)
	{
		if (const auto entity = active_rule_entity(found->second.active))
			entity.get_mut<ActiveRuleEffect>().clocks.erase(key);
	}
	else if (const auto owner = entities.find(found->second.ship); owner != entities.end())
		if (auto *runtime = owner->second.try_get_mut<RuleRuntime>())
			runtime->executions.erase(key);
	rule_clock_addresses.erase(found);
	rule_clock_capacity_cache.reset();
}
struct FrozenRuleSourceFrame
{
	std::string key;
	RuleEvaluation evaluation;
	bool native_eligible = false;
	bool active_owner = false;
	bool new_clock = false;
};
void discover_active_rule_clocks()
{
	rebuild_rule_clock_addresses();
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		const auto source = active_native_rule_source(effect);
		if (!source.eligible || !source.bindings)
			continue;
		for (const auto &binding : *source.bindings)
		{
			const auto context = bind_rule_context(binding, source.context);
			const auto key = rule_key(source.key, binding.id, context.subject);
			if (find_rule_execution(key))
				continue;
			if (auto *state = ensure_rule_execution(source, binding, context, true))
			{
				state->eligible = true;
				state->active = true;
				state->output = rule_templates.at(binding.rule).funding ? 0 : 1000;
			}
		}
	}
}
void retire_active_rule_effects()
{
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::vector<std::uint64_t> expired;
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.expiry <= now || !active_rule_target_available(effect))
			expired.push_back(effect.id);
	}
	for (const auto id : expired)
	{
		const auto entity = active_rule_entity(id);
		if (!entity)
			continue;
		const auto &owner = entity.get<ActiveRuleEffect>();
		std::vector<RuleExecutionState> clocks;
		for (const auto &[key, state] : owner.clocks)
			clocks.push_back(state);
		for (auto &state : clocks)
		{
			retire_rule_state(state, true);
			rule_clock_addresses.erase(state.key);
		}
		if (entity.is_alive())
			erase_active_rule_effect(entity);
		if (effect_commit_epoch == std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Effect commit epoch exhausted");
		++effect_commit_epoch;
	}
	std::erase_if(rule_origins,
	              [&](const auto &entry)
	              {
		              if (rule_origin_live(entry.first))
			              return false;
		              mark_custom_property_rates_dirty();
		              return true;
	              });
}
void prepare_rule_sources()
{
	clear_rule_upkeep();
	rebuild_rule_clock_addresses();
	retire_active_rule_effects();
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto sources = lifecycle_rule_sources();
	prune_custom_property_rates(sources);
	std::set<std::string> seen;
	std::vector<FrozenRuleSourceFrame> frames;
	for (const auto &source : sources)
		if (source.bindings)
			for (const auto &binding : *source.bindings)
			{
				const auto &rule = rule_templates.at(binding.rule);
				if (rule.trigger != RuleTrigger::Maintain && rule.trigger != RuleTrigger::Tick &&
				    rule.trigger != RuleTrigger::Occupancy && rule.trigger != RuleTrigger::Activate &&
				    rule.trigger != RuleTrigger::Deactivate)
					continue;
				auto context = bind_rule_context(binding, source.context);
				const bool eligible = source.eligible && rule_binding_matches(binding, source.context);
				const bool fresh = !find_rule_execution(rule_key(source.key, binding.id, context.subject));
				auto *state = ensure_rule_execution(source, binding, context, eligible);
				if (!state)
					continue;
				seen.insert(state->key);
				context.tick = now;
				context.elapsed = std::max<std::int64_t>(0, now - state->started);
				const bool retarget = rule.phase == RulePhase::Maintained && state->active &&
				                      !same_rule_application(rule, state->applied_context, context);
				context.output =
				    rule.funding ? (!retarget && state->lease_tick == now ? state->lease_permille : 0) : 1000;
				state->context = context;
				frames.push_back({state->key,
				                  eligible ? evaluate_source_rule(rule, context)
				                           : RuleEvaluation{RuleEvaluationStatus::Unmatched, {}},
				                  eligible, source.active_effect != 0, fresh});
			}
	// Every source evaluation above observes the same pre-commit phase.
	std::vector<std::string> retired;
	for (const auto &[key, address] : rule_clock_addresses)
		if (!seen.contains(key))
			retired.push_back(key);
	for (const auto &key : retired)
	{
		if (auto *state = find_rule_execution(key))
			retire_rule_state(*state, true);
		erase_rule_execution(key);
	}
	prune_custom_property_rates(sources);
	for (auto &frame : frames)
	{
		auto *state = find_rule_execution(frame.key);
		if (!state)
			continue;
		const auto &rule = rule_templates.at(state->definition);
		bool eligible = frame.native_eligible;
		if (rule.phase == RulePhase::Maintained || rule.phase == RulePhase::Periodic)
			eligible = eligible && frame.evaluation.status != RuleEvaluationStatus::Unmatched;
		if (!eligible)
		{
			if (rule.phase == RulePhase::Periodic && frame.native_eligible && state->next <= now && state->last != now)
			{
				state->next = now + rule.period_ticks;
				state->last = now;
			}
			retire_rule_state(*state, true);
			continue;
		}
		const bool rising = !state->eligible;
		if (rising)
		{
			const bool rearming_stop = state->stopped || state->stop_tick >= 0;
			if ((rule.phase == RulePhase::Periodic && rearming_stop) ||
			    (rule.phase == RulePhase::Instant && (!frame.active_owner || !frame.new_clock)))
			{
				state->next = now + rule.period_ticks;
			}
			state->stopped = false;
			state->stop_tick = -1;
		}
		state->eligible = true;
		if (state->stop_tick >= 0 && state->stop_tick <= now)
		{
			withdraw_rule_grants(*state);
			state->stopped = true;
			state->stop_tick = -1;
		}
		if (state->stopped)
		{
			state->output = 0;
			continue;
		}
		if (rule.trigger == RuleTrigger::Activate)
		{
			if (rising)
				enqueue_rule(state->definition, state->key, state->context, RuleEventPhase::Source, now);
			continue;
		}
		if (rule.trigger == RuleTrigger::Deactivate)
			continue;
		auto output = rule.funding ? (state->lease_tick == now ? state->lease_permille : 0) : 1000;
		if (rule.phase == RulePhase::Maintained && state->active &&
		    !same_rule_application(rule, state->applied_context, state->context))
		{
			withdraw_rule_grants(*state);
			state->credits.clear();
			state->lease_tick = -1;
			state->lease_permille = 0;
			output = rule.funding ? 0 : 1000;
		}
		if (rule.phase == RulePhase::Periodic)
		{
			state->output = output;
			state->active = true;
		}
		if (frame.evaluation.status != RuleEvaluationStatus::Ready)
		{
			begin_custom_property_batch();
			if (rule.phase == RulePhase::Maintained)
				capture_rule_custom_targets(rule, state->context);
			if (rule.phase != RulePhase::Periodic)
				state->output = 0;
			finish_custom_property_batch();
			state->blocker = frame.evaluation.status == RuleEvaluationStatus::Invalid
			                     ? "Invalid rule arithmetic or missing fact"
			                     : "";
			continue;
		}
		if (rule.phase == RulePhase::Maintained && state->last != now)
		{
			begin_custom_property_batch();
			capture_rule_custom_targets(rule, state->context);
			const bool entry =
			    !frame.evaluation.quote.effects.costs.empty() || frame.evaluation.quote.effects.energy > 0;
			bool failed_entry = false;
			const bool update = state->active && (!rule.values.empty() || output != state->output);
			if (state->active || output > 0 || (rule.funding && entry))
			{
				auto quote = frame.evaluation;
				if (state->active)
				{
					quote.quote.effects.energy = 0;
					quote.quote.effects.costs.clear();
					// Native topology/expiry is installed once. Only existing modifier
					// rows have a supported live update; no expired grant is recreated.
					std::erase_if(quote.quote.effects.operations,
					              [&](const auto &operation)
					              {
						              return !update || !maintained_modifier_exists(*state, operation);
					              });
				}
				const auto failure = state->active && quote.quote.effects.operations.empty()
				                         ? std::string{}
				                         : execute_rule_quote(quote, state->context, state->key, true, output);
				// Transactions can replace an active effect component: reacquire by logical key.
				state = find_rule_execution(frame.key);
				if (!state)
				{
					finish_custom_property_batch();
					continue;
				}
				state->blocker = failure;
				if (failure.empty())
				{
					state->active = true;
					state->output = output;
					state->applied_context = rule_applied_context(state->context);
				}
				else
				{
					state->output = 0;
					failed_entry = !state->active && entry;
				}
			}
			else
				state->output = 0;
			state->last = now;
			finish_custom_property_batch();
			if (failed_entry)
				continue;
		}
		else if (rule.phase == RulePhase::Periodic)
			state->output = output;
		if (rule.funding && rule.phase == RulePhase::Maintained && !state->active &&
		    (frame.evaluation.quote.effects.energy > 0 || !frame.evaluation.quote.effects.costs.empty()))
			continue;
		if (rule.funding && !state->stopped)
			stage_rule_upkeep(*state, frame.evaluation.quote.upkeep, rule.funding->shortage);
	}
}
void refresh_rule_eligibility()
{
	// Retirement can remove the native owner of another maintained source. Close
	// that dependency chain without evaluating, activating or paying any rule.
	for (std::size_t pass = 0; pass <= rule_instance_limit; ++pass)
	{
		rebuild_rule_clock_addresses();
		const auto before_epoch = effect_commit_epoch;
		const auto before_clocks = rule_clock_addresses.size();
		const auto before_active = active_rule_index.size();
		retire_active_rule_effects();
		std::map<std::string, bool> available;
		const auto sources = lifecycle_rule_sources();
		prune_custom_property_rates(sources);
		for (const auto &source : sources)
			available.emplace(source.key, source.eligible);
		std::vector<std::string> keys;
		for (const auto &[key, address] : rule_clock_addresses)
			keys.push_back(key);
		for (const auto &key : keys)
		{
			const auto *current = find_rule_execution(key);
			if (!current)
				continue;
			const auto source = available.find(current->source_key);
			if (source == available.end() || !source->second)
			{
				auto retired = *current;
				retire_rule_state(retired, true);
				if (source == available.end())
					erase_rule_execution(key);
				else if (auto *remaining = find_rule_execution(key))
					*remaining = std::move(retired);
			}
		}
		prune_custom_property_rates(sources);
		rebuild_rule_clock_addresses();
		if (before_epoch == effect_commit_epoch && before_clocks == rule_clock_addresses.size() &&
		    before_active == active_rule_index.size())
			return;
	}
	throw std::runtime_error("rules: native source retirement did not converge");
}
void step_rule_clocks()
{
	refresh_rule_eligibility();
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	struct Due
	{
		std::string key, definition;
		NativeRuleContext context;
	};
	std::vector<Due> due;
	for (const auto &source : lifecycle_rule_sources())
		if (source.eligible && source.bindings)
			for (const auto &binding : *source.bindings)
			{
				const auto &rule = rule_templates.at(binding.rule);
				if (rule.phase != RulePhase::Periodic ||
				    (rule.trigger != RuleTrigger::Tick && rule.trigger != RuleTrigger::Occupancy))
					continue;
				auto context = bind_rule_context(binding, source.context);
				const auto key = rule_key(source.key, binding.id, context.subject);
				const bool fresh = !find_rule_execution(key);
				auto *state = ensure_rule_execution(source, binding, context, true);
				if (!state)
					continue;
				if (fresh)
				{
					state->eligible = true;
					state->active = true;
					state->output = rule.funding ? 0 : 1000;
				}
				if (!state->eligible || state->stopped || state->next > now || state->last == now)
					continue;
				state->next = now + rule.period_ticks;
				state->last = now;
				if (!rule_binding_matches(binding, source.context) || (rule.funding && !state->output))
					continue;
				context.tick = now;
				context.elapsed = std::max<std::int64_t>(0, now - state->started);
				context.output = state->output;
				state->context = context;
				due.push_back({key, state->definition, std::move(context)});
			}
	// Queue first; the common phase drain freezes all due quotes before committing.
	for (const auto &event : due)
		enqueue_rule(event.definition, event.key, event.context, RuleEventPhase::Periodic, now);
}
