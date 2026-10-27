// Included inside World::Impl. Views retain native lifetime/eligibility authority;
// provenance records only the frozen application context, never a second lifetime.
using RuleOriginKey = std::tuple<EntityId, bool, std::string>;
std::map<RuleOriginKey, NativeRuleContext> rule_origins;
inline static constexpr std::size_t rule_origin_limit = 32768;
bool native_rule_provenance_required(const std::string &definition) const
{
	if (const auto found = native_effect_rules.find(definition);
	    found != native_effect_rules.end() && !found->second.empty())
		return true;
	if (const auto found = shield_templates.find(definition); found != shield_templates.end())
		return std::any_of(found->second.contributions.begin(), found->second.contributions.end(),
		                   [](const auto &contribution)
		                   {
			                   return !contribution.shield.rules.empty();
		                   });
	return false;
}

RuleActor rule_origin_target(const EffectGroupInvocation &frame, const std::string &role) const
{
	if (frame.rule_context)
		return rule_actor(*frame.rule_context, role);
	if (role == "attacker" || role == "source" || role == "instigator")
		return {frame.attacker, frame.original_equipment};
	RuleActor actor{frame.protected_ship};
	if (frame.hit_context && frame.hit_context->target == actor.entity)
		actor.ordnance = frame.hit_context->ordnance;
	return actor;
}
bool rule_origin_live(const RuleOriginKey &key, const PreparedEffectGroup *prepared = nullptr) const
{
	const auto &[id, ordnance, source] = key;
	if (ordnance || !entities.contains(id))
		return false;
	const auto ship = entities.at(id);
	if (process_origin_live(id, source, prepared))
		return true;
	const auto *shields =
	    prepared && prepared->shields.contains(id) ? &prepared->shields.at(id) : ship.try_get<ShieldComponent>();
	if (shields)
		for (const auto &contribution : shields->contributions)
			if (contribution.source == source && native_rule_provenance_required(contribution.definition_id))
				return true;
	if (prepared && prepared->properties.contains(id))
	{
		for (const auto &row : prepared->properties.at(id).applications)
			if (row.source == source && native_rule_provenance_required(row.definition))
				return true;
	}
	else if (const auto *owner = ship.try_get<PropertyEffectOwner>())
		for (auto at = owner->grants.lower_bound({source, {}}); at != owner->grants.end() && at->first.first == source;
		     ++at)
			if (native_rule_provenance_required(at->second.get<ActiveEffectIdentity>().definition))
				return true;

	const auto *abilities =
	    prepared && prepared->abilities.contains(id) ? &prepared->abilities.at(id) : ship.try_get<AbilityRuntime>();
	if (abilities)
		for (const auto &[name, grant] : abilities->grants)
			if (grant.parent == source && native_rule_provenance_required(grant.grant_effect))
				return true;
	return false;
}
bool native_property_rate_owner_live(const NativeRuleContext &context) const
{
	if (context.owner_effect_kind == NativeEffectKind::None)
		return true;
	if (context.owner_effect_kind == NativeEffectKind::Process)
	{
		const auto address = custom_property_address(context.owner);
		const auto *runtime = process_runtime(address);
		if (!runtime)
			return false;
		for (const auto &[id, state] : runtime->processes)
			if (state.generation == context.owner_effect_id &&
			    (state.expiry < 0 || state.expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick)))
				return (!state.source.empty() && state.source == context.owner_grant) ||
				       (state.admitted && process_run_source(address, state) == context.owner_grant);
		return false;
	}
	if (context.owner_effect_kind == NativeEffectKind::Periodic)
	{
		const auto effect = active_rule_entity(context.owner_effect_id);
		return effect && effect.get<ActiveRuleEffect>().expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick) &&
		       active_rule_target_available(effect.get<ActiveRuleEffect>());
	}
	if (!rule_origin_live({context.owner.entity, context.owner.ordnance, context.owner_grant}))
		return false;
	const auto found = entities.find(context.owner.entity);
	if (found == entities.end())
		return false;
	const auto ship = found->second;
	if (context.owner_effect_kind == NativeEffectKind::Modifier)
	{
		const auto *owner = ship.try_get<PropertyEffectOwner>();
		if (!owner || !owner->applications.contains(context.owner_effect_id))
			return false;
		const auto effect = owner->applications.at(context.owner_effect_id);
		const auto expiry = effect.get<EffectLifetime>().expiry;
		return effect.get<ActiveEffectIdentity>().source == context.owner_grant &&
		       (expiry < 0 || expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick));
	}
	if (context.owner_effect_kind == NativeEffectKind::Shield)
	{
		const auto *shields = ship.try_get<ShieldComponent>();
		return shields &&
		       std::any_of(shields->contributions.begin(), shields->contributions.end(),
		                   [&](const auto &row)
		                   {
			                   return row.effect_id == context.owner_effect_id && row.source == context.owner_grant &&
			                          (row.expiry < 0 || row.expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick));
		                   });
	}
	if (context.owner_effect_kind == NativeEffectKind::Ability)
	{
		const auto *abilities = ship.try_get<AbilityRuntime>();
		return abilities &&
		       std::any_of(abilities->grants.begin(), abilities->grants.end(),
		                   [&](const auto &row)
		                   {
			                   return row.second.generation == context.owner_effect_id &&
			                          row.second.parent == context.owner_grant &&
			                          (row.second.expiry < 0 ||
			                           row.second.expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick));
		                   });
	}
	return false;
}
std::size_t rule_clock_capacity(const std::vector<ActiveRuleEffect> *prepared = nullptr) const
{
	if (!prepared && rule_clock_capacity_cache)
		return *rule_clock_capacity_cache;
	std::size_t clocks = 0;
	for (const auto &[id, ship] : entities)
		if (const auto *runtime = ship.try_get<RuleRuntime>())
			clocks += runtime->executions.size();
	const auto include = [&](const ActiveRuleEffect &effect)
	{
		clocks += effect.clocks.size();
		if (const auto found = periodic_templates.find(effect.definition); found != periodic_templates.end())
			for (const auto &binding : found->second.rules)
				if (std::none_of(effect.clocks.begin(), effect.clocks.end(),
				                 [&](const auto &entry)
				                 {
					                 return entry.second.binding == binding.id &&
					                        entry.second.definition == binding.rule;
				                 }))
					++clocks;
	};
	if (prepared)
		for (const auto &effect : *prepared)
			include(effect);
	else
	{
		for (auto effect_entity : active_rule_rows())
		{
			const auto &effect = effect_entity.get<ActiveRuleEffect>();
			include(effect);
		}
		rule_clock_capacity_cache = clocks;
	}
	return clocks;
}
std::string rule_origin_failure(const PreparedEffectGroup &prepared) const
{
	// Read-only validation must not materialize every condition and clock for
	// each reaction. Staged ownership still receives a complete projected check.
	if (rule_clock_capacity(prepared.active_effects ? &*prepared.active_effects : nullptr) > rule_instance_limit)
		return "Rule clock capacity exhausted";
	std::set<RuleOriginKey> keys;
	for (const auto &[key, context] : rule_origins)
		if (rule_origin_live(key, &prepared))
			keys.insert(key);
	for (const auto &frame : prepared.invocations)
		for (const auto &operation : frame.execution.operations)
			if (operation.kind == EffectOperationKind::Apply && native_rule_provenance_required(operation.effect))
			{
				const auto actor = rule_origin_target(frame, operation.target);
				RuleOriginKey key{actor.entity, actor.ordnance, frame.source + "." + operation.grant};
				if (rule_origin_live(key, &prepared))
					keys.insert(std::move(key));
			}
	return keys.size() > rule_origin_limit ? "Native rule provenance capacity exhausted" : std::string{};
}
void remember_rule_origins(const PreparedEffectGroup &prepared)
{
	std::erase_if(rule_origins,
	              [&](const auto &entry)
	              {
		              if (rule_origin_live(entry.first))
			              return false;
		              mark_custom_property_rates_dirty();
		              return true;
	              });
	for (const auto &frame : prepared.invocations)
		for (const auto &operation : frame.execution.operations)
			if (operation.kind == EffectOperationKind::Apply && native_rule_provenance_required(operation.effect))
			{
				const auto actor = rule_origin_target(frame, operation.target);
				RuleOriginKey key{actor.entity, actor.ordnance, frame.source + "." + operation.grant};
				if (!rule_origin_live(key))
					continue;
				NativeRuleContext context = frame.rule_context.value_or(NativeRuleContext{});
				if (!frame.rule_context)
				{
					context.owner = {frame.protected_ship};
					context.source = context.instigator = {frame.attacker, frame.original_equipment};
					context.subject = context.target = actor;
					context.hit = frame.hit_context.value_or(rule_hit_context(actor));
					context.tick = prepared.tick;
				}
				rule_origins.insert_or_assign(std::move(key), std::move(context));
			}
	if (prepared.active_effects)
		discover_active_rule_clocks();
	if (!retired_custom_property_rates.empty() || custom_property_native_rates_dirty)
		prune_custom_property_rates(lifecycle_rule_sources());
}
NativeRuleContext native_rule_origin(const RuleActor &owner, const std::string &source,
                                     NativeEffectKind kind = NativeEffectKind::None, std::uint64_t instance = 0) const
{
	const auto found = rule_origins.find({owner.entity, owner.ordnance, source});
	auto context = found == rule_origins.end() ? NativeRuleContext{} : found->second;
	if (found == rule_origins.end())
		context.source = context.instigator = owner;
	context.owner = context.subject = context.target = owner;
	context.owner_grant = source;
	context.owner_effect_kind = kind;
	context.owner_effect_id = instance;
	context.hit = rule_hit_context(owner);
	context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	context.property_role.clear();
	context.property_filter.clear();
	context.capture_owner = false;
	std::erase_if(context.captured,
	              [](const auto &capture)
	              {
		              return capture.fact.role == RuleRole::Owner;
	              });
	return context;
}
NativeRuleContext merge_rule_event_context(const NativeRuleSource &source, const NativeRuleContext &event) const
{
	auto context = source.context;
	context.owner = source.owner;
	context.subject = event.subject;
	context.target = event.target;
	context.instigator = event.instigator;
	context.hit = event.hit;
	context.tick = event.tick;
	context.elapsed = event.elapsed;
	context.presented = event.presented;
	context.modified = event.modified;
	context.negated = event.negated;
	context.absorbed = event.absorbed;
	context.residual = event.residual;
	context.strength = event.strength;
	context.inflicted = event.inflicted;
	context.output = event.output;
	std::erase_if(context.captured,
	              [&](const auto &capture)
	              {
		              return capture.fact.role == RuleRole::Instigator ||
		                     (capture.fact.role == RuleRole::Owner && !context.capture_owner);
	              });
	for (const auto &capture : event.captured)
		if (capture.fact.role == RuleRole::Instigator)
			context.captured.push_back(capture);
	return context;
}
// Event producers own capture/phase policy; native binding traversal is shared.
template <class Callback>
void visit_rule_event_bindings(const NativeRuleSource &source, RuleTrigger trigger, const NativeRuleContext &event,
                               Callback &&callback) const
{
	if (!source.eligible || !source.bindings)
		return;
	for (const auto &binding : *source.bindings)
	{
		const auto found = rule_templates.find(binding.rule);
		if (found == rule_templates.end() || found->second.trigger != trigger)
			continue;
		auto context = merge_rule_event_context(source, event);
		if (!rule_binding_matches(binding, context))
			continue;
		context = bind_rule_context(binding, std::move(context));
		callback(rule_key(source.key, binding.id, event.subject), found->second, std::move(context));
	}
}
NativeRuleSource active_native_rule_source(const ActiveRuleEffect &effect, bool retiring = false) const
{
	NativeRuleSource source;
	source.kind = RuleOwnerKind::Effect;
	source.key = "active-effect:" + std::to_string(effect.id);
	source.active_effect = effect.id;
	source.owner = rule_hit_actor(effect.context);
	source.context = effect.origin;
	source.context.owner = source.context.subject = source.context.target = source.owner;
	source.context.owner_grant = effect.source;
	source.context.owner_effect_kind = NativeEffectKind::Periodic;
	source.context.owner_effect_id = effect.id;
	source.context.hit = effect.context;
	source.context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	source.context.property_role.clear();
	source.context.property_filter.clear();
	source.context.capture_owner = false;
	std::erase_if(source.context.captured,
	              [](const auto &capture)
	              {
		              return capture.fact.role == RuleRole::Owner;
	              });
	if (const auto found = periodic_templates.find(effect.definition); found != periodic_templates.end())
		source.bindings = &found->second.rules;
	source.eligible = source.bindings && effect.expiry > source.context.tick &&
	                  (active_rule_target_available(effect) || (retiring && bool(payload_target(effect.context)))) &&
	                  rule_lease_available(effect.source);
	return source;
}
std::vector<NativeRuleSource> rule_sources(const RuleActor &actor, bool retiring = false) const
{
	if (!actor.ordnance)
	{
		const auto found = entities.find(actor.entity);
		return found == entities.end() ? std::vector<NativeRuleSource>{} : rule_sources(found->second, retiring);
	}
	std::vector<NativeRuleSource> sources;
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.context.ordnance && effect.context.target == actor.entity)
		{
			auto source = active_native_rule_source(effect, retiring);
			if (source.bindings && !source.bindings->empty())
				sources.push_back(std::move(source));
		}
	}
	std::sort(sources.begin(), sources.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.key < b.key;
	          });
	return sources;
}
std::vector<NativeRuleSource> rule_sources(flecs::entity ship, bool retiring = false) const
{
	std::vector<NativeRuleSource> result;
	if (!ship)
		return result;
	if (!ship.has<StableId>())
	{
		if (const auto *projectile = ship.try_get<Projectile>())
			return rule_sources(RuleActor{projectile->id, {}, {}, true}, retiring);
		return result;
	}
	const auto id = ship.get<StableId>().id;
	const auto &compiled = definition(id);
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const bool alive = (retiring || !combat_dead(ship)) && !departed(id);
	const auto add = [&](RuleOwnerKind kind, std::string key, RuleActor owner, NativeRuleContext context,
	                     const std::vector<RuleBinding> &bindings, bool eligible)
	{
		if (bindings.empty())
			return;
		context.owner = owner;
		context.tick = tick;
		result.push_back({kind, std::move(key), std::move(owner), std::move(context), &bindings, eligible});
	};
	const auto passive = [&](RuleActor owner)
	{
		NativeRuleContext context;
		context.owner = context.source = context.instigator = context.subject = context.target = owner;
		context.hit = rule_hit_context(owner);
		return context;
	};
	const RuleActor body{id};
	add(RuleOwnerKind::Equipment, "entity:" + std::to_string(id), body, passive(body), compiled.rules.entity, alive);
	for (const auto &[equipment, bindings] : compiled.rules.equipment)
	{
		RuleActor owner{id, equipment};
		const bool exists = ship.has<EquipmentIndex>() && bool(instance(ship, equipment));
		// Enabled/disabled is an authored fact, not an implicit lifecycle switch.
		add(RuleOwnerKind::Equipment, "equipment:" + std::to_string(id) + ":" + equipment, owner, passive(owner),
		    bindings, alive && exists);
	}
	const auto *personnel = ship.try_get<PersonnelState>();
	if (personnel)
	{
		const auto &definitions = compiled.personnel;
		for (std::size_t i = 0; i < personnel->officers.size(); ++i)
		{
			const auto &officer = definitions.officers.at(i);
			const auto &state = personnel->officers[i];
			RuleActor owner{id, {}, officer.id};
			const auto *duty = personnel_duty(id, state.duty);
			const bool healthy = alive && officer_healthy(state);
			const bool aboard = healthy && state.present && duty && away_officer_location(id, officer.id).empty();
			if (const auto found = compiled.rules.officers.find(officer.id); found != compiled.rules.officers.end())
				add(RuleOwnerKind::Officer, "officer:" + std::to_string(id) + ":" + officer.id, owner, passive(owner),
				    found->second, aboard);
			if (const auto found = compiled.rules.officer_lifetime.find(officer.id);
			    found != compiled.rules.officer_lifetime.end())
				add(RuleOwnerKind::Officer, "officer-lifetime:" + std::to_string(id) + ":" + officer.id, owner,
				    passive(owner), found->second, alive && state.condition != "dead");
			// Merge native participation routes before creating sources. An all-property
			// route subsumes a repair-only route to the same role and target.
			std::map<std::pair<std::string, std::string>, std::string> targets;
			const auto target =
			    [&](const std::string &equipment, const std::string &role, const std::string &property = std::string{})
			{
				for (const auto &trait : officer.traits)
					for (const auto &effect : definitions.traits.at(trait).effects)
						for (const auto &contribution : effect.contributions)
							if ((role.empty() || contribution.role == role) &&
							    (property.empty() || contribution.property == property))
							{
								const auto key = std::pair{equipment, contribution.role};
								const auto found = targets.find(key);
								if (found == targets.end())
									targets.emplace(key, property);
								else if (property.empty())
									found->second.clear();
							}
			};
			if (aboard)
				for (const auto &[equipment, roles] : duty->effects)
					for (const auto &role : roles)
						target(equipment, role);
			if (healthy)
				for (std::size_t mission = 0; mission < away_missions.size(); ++mission)
				{
					const auto &away = away_missions[mission];
					if (away_ship(away_definitions[mission]) != id || !away.deployed())
						continue;
					if (duty && !duty->team.empty() &&
					    std::find(away.accompanying.begin(), away.accompanying.end(), officer.id) !=
					        away.accompanying.end())
						for (const auto &[slot, asset] : away.assignments)
							if (asset == "team:" + duty->team)
								if (const auto roles = duty->effects.find(duty->team); roles != duty->effects.end())
									for (const auto &role : roles->second)
										target(duty->team, role);
					for (const auto &[slot, asset] : away.assignments)
						if (asset == "officer:" + officer.id)
						{
							target("away:" + away_definitions[mission].id + ":officer:" + officer.id, {}, "repair");
							for (const auto &[team_slot, team_asset] : away.assignments)
								if (team_asset.starts_with("team:"))
								{
									const auto team = away_equipment(id, team_asset.substr(5));
									if (team && team.get<CombatMechanism>()
									                .definition.get<TeamDefinition>()
									                .capabilities.contains("repair"))
										target(team_asset.substr(5), {}, "repair");
								}
						}
				}
			for (const auto &trait_id : officer.traits)
			{
				const auto &trait = definitions.traits.at(trait_id);
				for (const auto &[address, property] : targets)
				{
					const auto &[equipment, role] = address;
					bool relevant = false;
					for (const auto &effect : trait.effects)
						for (const auto &contribution : effect.contributions)
							relevant |=
							    contribution.role == role && (property.empty() || contribution.property == property);
					if (!relevant)
						continue;
					auto context = passive(owner);
					context.subject = context.target = {id, equipment};
					context.hit = rule_hit_context(context.subject);
					context.property_role = role;
					context.property_filter = property;
					add(RuleOwnerKind::Officer,
					    "trait:" + std::to_string(id) + ":" + officer.id + ":" + trait_id + ":" + equipment + ":" +
					        role + ":" + property,
					    owner, std::move(context), trait.rules, true);
				}
			}
		}
	}
	const auto native = [&](const std::string &definition, const std::string &source, std::string key, RuleActor owner,
	                        bool eligible, NativeEffectKind kind, std::uint64_t instance)
	{
		const auto found = native_effect_rules.find(definition);
		if (found != native_effect_rules.end())
			add(RuleOwnerKind::Effect, std::move(key), owner, native_rule_origin(owner, source, kind, instance),
			    found->second, alive && eligible && rule_lease_available(source));
	};
	if (const auto *shields = ship.try_get<ShieldComponent>())
	{
		std::set<std::uint64_t> seen;
		for (const auto &contribution : shields->contributions)
			if (seen.insert(contribution.effect_id).second)
				native(contribution.definition_id, contribution.source,
				       "shield-effect:" + std::to_string(id) + ":" + std::to_string(contribution.effect_id),
				       {id, contribution.equipment}, contribution.expiry < 0 || contribution.expiry > tick,
				       NativeEffectKind::Shield, contribution.effect_id);
	}
	if (const auto *properties = ship.try_get<PropertyEffectOwner>())
		for (const auto &[application, effect] : properties->applications)
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			const auto bindings = native_effect_rules.find(identity.definition);
			if (bindings == native_effect_rules.end() || bindings->second.empty())
				continue;
			const auto expiry = effect.get<EffectLifetime>().expiry;
			native(identity.definition, identity.source,
			       "modifier-effect:" + std::to_string(id) + ":" + std::to_string(application),
			       custom_property_actor(id, effect.get<EffectTarget>().equipment_name), expiry < 0 || expiry > tick,
			       NativeEffectKind::Modifier, application);
		}
	if (const auto *abilities = ship.try_get<AbilityRuntime>())
		for (const auto &[key, grant] : abilities->grants)
			if (!grant.grant_effect.empty())
			{
				RuleActor owner{id};
				if (grant.source == "officer")
					owner.officer = grant.owner;
				else
					owner.equipment = grant.owner;
				native(grant.grant_effect, grant.parent,
				       "ability-effect:" + std::to_string(id) + ":" + std::to_string(grant.generation), owner,
				       grant.expiry < 0 || grant.expiry > tick, NativeEffectKind::Ability, grant.generation);
			}
	for (const auto &address : custom_property_addresses(id))
		if (const auto *runtime = process_runtime(address))
			for (const auto &[slot, state] : runtime->processes)
			{
				const auto owner = process_actor(address);
				const auto lifetime = state.expiry < 0 || state.expiry > tick;
				const auto add_process_source =
				    [&](const std::string &definition, const std::string &key, NativeRuleContext context, bool eligible)
				{
					const auto bindings = native_effect_rules.find(definition);
					if (bindings != native_effect_rules.end())
						add(RuleOwnerKind::Process, key, owner, std::move(context), bindings->second,
						    alive && eligible && rule_lease_available(state.source));
				};
				if (state.admitted)
					add_process_source(state.definition, process_run_source(address, state),
					                   process_context(address, state),
					                   lifetime && process_run_available(process_context(address, state)));
				if (!state.grant_effect.empty() && state.parent_process.empty())
				{
					auto context = state.origin;
					context.owner = context.subject = context.target = owner;
					context.owner_grant = state.source;
					context.owner_effect_kind = NativeEffectKind::Process;
					context.owner_effect_id = state.generation;
					context.hit = rule_hit_context(owner);
					add_process_source(state.grant_effect, "process-grant:" + process_instance_key(address, state),
					                   std::move(context), lifetime);
				}
			}
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.context.target == id && !effect.context.ordnance)
		{
			auto source = active_native_rule_source(effect, retiring);
			source.eligible = source.eligible && alive;
			if (source.bindings && !source.bindings->empty())
				result.push_back(std::move(source));
		}
	}
	if (const auto *runtime = ship.try_get<RuleRuntime>())
		for (const auto &region : runtime->regions)
			if (const auto found = region_rule_bindings.find(region); found != region_rule_bindings.end())
			{
				NativeRuleContext context;
				context.subject = context.target = body;
				context.hit = rule_hit_context(body);
				context.region = region;
				add(RuleOwnerKind::Region, "region:" + region + ":" + std::to_string(id), {}, std::move(context),
				    found->second, alive);
			}
	std::sort(result.begin(), result.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.key < b.key;
	          });
	return result;
}
