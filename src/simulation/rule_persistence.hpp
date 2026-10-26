// Included inside World::Impl after common rule lifecycle/source adapters.
static void rule_saved_fields(const Json &value, std::initializer_list<const char *> fields)
{
	if (!value.is_object() || value.size() != fields.size())
		throw std::runtime_error("rules: invalid saved object layout");
	for (const auto field : fields)
		if (!value.contains(field))
			throw std::runtime_error("rules: missing saved field");
}
static std::int64_t rule_saved_integer(const Json &value, std::int64_t minimum = 0,
                                       std::int64_t maximum = 9007199254740991LL)
{
	return checked_integer(value, minimum, maximum, "rules.saved");
}
static void rule_saved_text(const Json &value, std::size_t maximum = 4096, bool empty = true)
{
	if (!value.is_string() || value.get_ref<const std::string &>().size() > maximum ||
	    (!empty && value.get_ref<const std::string &>().empty()))
		throw std::runtime_error("rules: invalid saved identity");
}
static void rule_saved_bool(const Json &value)
{
	if (!value.is_boolean())
		throw std::runtime_error("rules: invalid saved boolean");
}
static bool saved_rule_key(const std::string &value)
{
	return value.size() == 69 && value.starts_with("rule.") &&
	       std::all_of(value.begin() + 5, value.end(),
	                   [](char c)
	                   {
		                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	                   });
}
void validate_saved_rule_actor(const Json &value) const
{
	rule_saved_fields(value, {"entity", "equipment", "officer", "ordnance", "kind", "compartment", "contribution",
	                          "contribution_name"});
	rule_saved_integer(value.at("entity"));
	rule_saved_integer(value.at("kind"), 0, static_cast<int>(PayloadOutcomeKind::Entity));
	rule_saved_integer(value.at("compartment"), -1);
	rule_saved_integer(value.at("contribution"));
	rule_saved_text(value.at("equipment"), 1024);
	rule_saved_text(value.at("officer"), 256);
	rule_saved_text(value.at("contribution_name"), 256);
	rule_saved_bool(value.at("ordnance"));
	const auto actor = value.get<RuleActor>();
	if ((!actor.entity && (!actor.equipment.empty() || !actor.officer.empty() || actor.ordnance)) ||
	    (actor.ordnance && (actor.kind != PayloadOutcomeKind::Entity || !actor.officer.empty())) ||
	    ((actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System) !=
	     (actor.compartment >= 0)) ||
	    (actor.kind == PayloadOutcomeKind::Shield ? !actor.contribution || actor.contribution_name.empty()
	                                              : actor.contribution || !actor.contribution_name.empty()) ||
	    (actor.kind == PayloadOutcomeKind::System && actor.equipment.empty()))
		throw std::runtime_error("rules: malformed saved native address");
	// A historical source/launch owner may no longer exist. Existing physical
	// addresses still have to name a valid authored compartment/system/officer.
	if (!actor.ordnance && entities.contains(actor.entity))
	{
		const auto ship = entities.at(actor.entity);
		if (!actor.officer.empty())
		{
			const auto &officers = definition(actor.entity).personnel.officers;
			if (std::none_of(officers.begin(), officers.end(),
			                 [&](const auto &officer)
			                 {
				                 return officer.id == actor.officer;
			                 }))
				throw std::runtime_error("rules: saved officer identity unavailable");
		}
		if (actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System)
		{
			const auto *structure = ship.try_get<CombatStructure>();
			if (!structure || actor.compartment >= static_cast<std::int64_t>(structure->compartments.size()))
				throw std::runtime_error("rules: saved compartment identity unavailable");
			if (actor.kind == PayloadOutcomeKind::System)
			{
				const auto found = structure->definition.equipment_compartments.find(actor.equipment);
				if (found == structure->definition.equipment_compartments.end() ||
				    structure->definition.compartment_ids.at(found->second) !=
				        static_cast<std::size_t>(actor.compartment))
					throw std::runtime_error("rules: saved system address mismatch");
			}
		}
	}
}
void validate_saved_rule_context(const Json &value) const
{
	rule_saved_fields(value, {"owner",
	                          "source",
	                          "instigator",
	                          "subject",
	                          "target",
	                          "hit",
	                          "property_role",
	                          "property_filter",
	                          "region",
	                          "owner_grant",
	                          "owner_effect_kind",
	                          "owner_effect_id",
	                          "capture_owner",
	                          "tick",
	                          "elapsed",
	                          "presented",
	                          "modified",
	                          "negated",
	                          "absorbed",
	                          "residual",
	                          "strength",
	                          "inflicted",
	                          "output",
	                          "captured"});
	for (const auto role : {"owner", "source", "instigator", "subject", "target"})
		validate_saved_rule_actor(value.at(role));
	const auto &hit = value.at("hit");
	rule_saved_fields(hit,
	                  {"target", "kind", "compartment", "equipment", "contribution", "contribution_name", "ordnance"});
	Json actor = hit;
	actor["entity"] = actor.at("target");
	actor.erase("target");
	actor["officer"] = "";
	validate_saved_rule_actor(actor);
	for (const auto field : {"property_role", "property_filter", "region"})
		rule_saved_text(value.at(field), 256);
	rule_saved_text(value.at("owner_grant"), 1024);
	rule_saved_integer(value.at("owner_effect_kind"), 0, static_cast<int>(NativeEffectKind::Periodic));
	rule_saved_integer(value.at("owner_effect_id"));
	rule_saved_bool(value.at("capture_owner"));
	for (const auto field :
	     {"tick", "elapsed", "presented", "modified", "negated", "absorbed", "residual", "strength", "inflicted"})
		rule_saved_integer(value.at(field));
	rule_saved_integer(value.at("output"), 0, 1000);
	const auto &captures = value.at("captured");
	if (!captures.is_array() || captures.size() > rule_expression_node_limit)
		throw std::runtime_error("rules: invalid saved captured layout");
	std::vector<RuleFactBinding> seen;
	for (const auto &saved : captures)
	{
		rule_saved_fields(saved, {"role", "kind", "key", "aux", "unit", "type", "value_type", "number", "text"});
		rule_saved_integer(saved.at("role"), 0, static_cast<int>(RuleRole::Protected));
		rule_saved_integer(saved.at("kind"), 0, static_cast<int>(RuleFactKind::Output));
		rule_saved_integer(saved.at("type"), 0, static_cast<int>(state_tree::Type::Message));
		rule_saved_integer(saved.at("value_type"), 0, static_cast<int>(state_tree::Type::Message));
		rule_saved_integer(saved.at("number"), -9007199254740991LL);
		for (const auto field : {"key", "aux", "unit", "text"})
			rule_saved_text(saved.at(field));
		const auto capture = saved.get<RuleCapturedFact>();
		const bool declared = std::any_of(rule_templates.begin(), rule_templates.end(),
		                                  [&](const auto &rule)
		                                  {
			                                  return std::find(rule.second.facts.begin(), rule.second.facts.end(),
			                                                   capture.fact) != rule.second.facts.end();
		                                  });
		if (!declared ||
		    (capture.fact.role != RuleRole::Owner && capture.fact.role != RuleRole::Source &&
		     capture.fact.role != RuleRole::Instigator) ||
		    std::find(seen.begin(), seen.end(), capture.fact) != seen.end() ||
		    (capture.value.type != state_tree::Type::Missing && capture.value.type != capture.fact.type) ||
		    (capture.value.type == state_tree::Type::Boolean && capture.value.number != 0 &&
		     capture.value.number != 1) ||
		    (capture.value.type != state_tree::Type::Text && !capture.value.text.empty()))
			throw std::runtime_error("rules: captured fact layout/type mismatch");
		seen.push_back(capture.fact);
	}
	const auto context = value.get<NativeRuleContext>();
	if ((context.owner_effect_kind == NativeEffectKind::None) != (context.owner_effect_id == 0) ||
	    (context.owner_effect_id && context.owner_grant.empty()))
		throw std::runtime_error("rules: malformed saved native owner identity");
	if (context.tick > static_cast<std::int64_t>(ecs.get<Clock>().tick))
		throw std::runtime_error("rules: saved context is from a future phase");
	if (!context.region.empty() && !region_rule_bindings.contains(context.region))
		throw std::runtime_error("rules: unknown saved region");
	const auto &properties = modifier_properties();
	if (!context.property_filter.empty() && std::none_of(properties.begin(), properties.end(),
	                                                     [&](const auto &property)
	                                                     {
		                                                     return property.id == context.property_filter;
	                                                     }))
		throw std::runtime_error("rules: unknown saved property filter");
}
void validate_saved_rule_state(const Json &value, std::int64_t now) const
{
	rule_saved_fields(value, {"key",     "definition",      "binding",        "source_key", "owner_kind",
	                          "context", "applied_context", "generation",     "started",    "next",
	                          "last",    "lease_tick",      "lease_permille", "stop_tick",  "eligible",
	                          "active",  "stopped",         "credits",        "output",     "blocker"});
	for (const auto field : {"key", "definition", "binding", "source_key"})
		rule_saved_text(value.at(field), 1024, false);
	rule_saved_text(value.at("blocker"), 4096);
	rule_saved_integer(value.at("owner_kind"), 0, static_cast<int>(RuleOwnerKind::Shield));
	rule_saved_integer(value.at("generation"), 1);
	rule_saved_integer(value.at("started"), 0, now);
	rule_saved_integer(value.at("next"));
	rule_saved_integer(value.at("last"), -1, now);
	rule_saved_integer(value.at("lease_tick"), -1, now + 1);
	rule_saved_integer(value.at("stop_tick"), -1, now + 1);
	rule_saved_integer(value.at("lease_permille"), 0, 1000);
	rule_saved_integer(value.at("output"), 0, 1000);
	for (const auto field : {"eligible", "active", "stopped"})
		rule_saved_bool(value.at(field));
	validate_saved_rule_context(value.at("context"));
	validate_saved_rule_context(value.at("applied_context"));
	const auto &credits = value.at("credits");
	if (!credits.is_array() || credits.size() > 4096)
		throw std::runtime_error("rules: saved credit capacity exceeded");
	std::set<std::tuple<EntityId, std::string, std::string>> seen;
	for (const auto &credit : credits)
	{
		rule_saved_fields(credit, {"payer", "store", "resource", "prepaid"});
		rule_saved_integer(credit.at("payer"), 1);
		rule_saved_integer(credit.at("prepaid"));
		rule_saved_text(credit.at("store"), 1024, false);
		rule_saved_text(credit.at("resource"), 256, false);
		const auto entry = credit.get<RuleWorkCredit>();
		(void)resource_catalogue->at(entry.resource);
		if (!seen.emplace(entry.payer, entry.store, entry.resource).second)
			throw std::runtime_error("rules: duplicate prepaid work credit");
		if (entities.contains(entry.payer))
			if (!named_stock(entities.at(entry.payer), entry.store).stock.accepts(entry.resource))
				throw std::runtime_error("rules: incompatible saved credit store");
	}
	const auto state = value.get<RuleExecutionState>();
	if (!rule_templates.contains(state.definition) || state.context.tick > now || state.next < state.started ||
	    (!state.eligible && (state.active || state.output || state.lease_tick != -1 || !state.credits.empty())) ||
	    (state.stopped && state.output) || (state.lease_tick == -1 && state.lease_permille))
		throw std::runtime_error("rules: invalid saved clock frontier");
	if (state.next > now + rule_templates.at(state.definition).period_ticks)
		throw std::runtime_error("rules: saved clock exceeds its native cadence frontier");
	const bool applied = state.active && rule_templates.at(state.definition).phase == RulePhase::Maintained;
	if (applied ? state.applied_context.owner != state.context.owner || !state.applied_context.captured.empty()
	            : state.applied_context != NativeRuleContext{})
		throw std::runtime_error("rules: invalid saved applied-context authority");
}
Json save_rules() const
{
	Json out = {{"schema", 1},
	            {"next_job", next_rule_job},
	            {"next_instance", next_rule_instance},
	            {"pending", pending_rules},
	            {"results", Json::array()},
	            {"active", active_rule_snapshot()},
	            {"ships", Json::array()},
	            {"origins", Json::array()}};
	for (const auto &[id, result] : rule_results)
		out["results"].push_back({{"id", id}, {"result", result}});
	for (const auto &[id, ship] : entities)
		out["ships"].push_back(
		    {{"entity", id}, {"state", ship.has<RuleRuntime>() ? ship.get<RuleRuntime>() : RuleRuntime{}}});
	for (const auto &[key, context] : rule_origins)
		if (rule_origin_live(key))
			out["origins"].push_back({{"target", std::get<0>(key)},
			                          {"ordnance", std::get<1>(key)},
			                          {"source", std::get<2>(key)},
			                          {"context", context}});
	return out;
}
void restore_rules(const Json &saved)
{
	rule_saved_fields(saved,
	                  {"schema", "next_job", "next_instance", "pending", "results", "active", "ships", "origins"});
	if (rule_saved_integer(saved.at("schema"), 1, 1) != 1)
		throw std::runtime_error("rules: unsupported save schema");
	const auto job_next = static_cast<std::uint64_t>(rule_saved_integer(saved.at("next_job"), 1));
	const auto instance_next = static_cast<std::uint64_t>(rule_saved_integer(saved.at("next_instance"), 1));
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto field : {"pending", "results", "active", "ships", "origins"})
		if (!saved.at(field).is_array())
			throw std::runtime_error("rules: saved roster is not an array");
	if (saved.at("pending").size() > rule_pending_limit || saved.at("results").size() > 1024 ||
	    saved.at("active").size() > rule_instance_limit || saved.at("ships").size() != entities.size() ||
	    saved.at("origins").size() > rule_origin_limit)
		throw std::runtime_error("rules: saved roster capacity exceeded");
	std::vector<PendingRuleExecution> pending;
	std::map<std::uint64_t, std::string> results;
	std::vector<ActiveRuleEffect> active;
	std::map<EntityId, RuleRuntime> runtimes;
	std::map<RuleOriginKey, NativeRuleContext> origins;
	std::set<std::uint64_t> jobs, instances;
	std::set<std::string> clocks;
	for (const auto &value : saved.at("pending"))
	{
		rule_saved_fields(value, {"id", "due", "phase", "rule", "source_key", "damage", "context"});
		const auto id = static_cast<std::uint64_t>(rule_saved_integer(value.at("id"), 1));
		rule_saved_integer(value.at("due"), std::max(std::int64_t{0}, now - 1), now + 1);
		rule_saved_integer(value.at("phase"), 0, static_cast<int>(RuleEventPhase::Periodic));
		rule_saved_integer(value.at("damage"), 0, 1000000);
		rule_saved_text(value.at("rule"), 256);
		rule_saved_text(value.at("source_key"), 1024, false);
		validate_saved_rule_context(value.at("context"));
		const auto job = value.get<PendingRuleExecution>();
		if (id >= job_next || !jobs.insert(id).second || job.context.tick > now ||
		    (job.rule.empty()
		         ? job.damage <= 0 || job.phase != RuleEventPhase::Payload
		         : !rule_templates.contains(job.rule) || job.damage != 0 || !saved_rule_key(job.source_key)))
			throw std::runtime_error("rules: invalid saved pending execution");
		if (!job.rule.empty())
		{
			const auto &rule = rule_templates.at(job.rule);
			const auto trigger = rule.trigger;
			const bool phase_matches =
			    job.phase == RuleEventPhase::Region ? trigger == RuleTrigger::Enter || trigger == RuleTrigger::Exit
			    : job.phase == RuleEventPhase::Source
			        ? trigger == RuleTrigger::Activate || trigger == RuleTrigger::Deactivate
			    : job.phase == RuleEventPhase::Periodic
			        ? trigger == RuleTrigger::Tick || trigger == RuleTrigger::Occupancy
			        : trigger == RuleTrigger::PostShieldDamage || trigger == RuleTrigger::PostCompartmentDamage ||
			              trigger == RuleTrigger::PostSystemDamage || trigger == RuleTrigger::PostEntityDamage ||
			              trigger == RuleTrigger::Destroy;
			if (!phase_matches)
				throw std::runtime_error("rules: saved event phase/trigger mismatch");
			if (job.context.capture_owner)
				for (const auto &fact : rule.facts)
					if ((fact.role == RuleRole::Owner || fact.role == RuleRole::Source ||
					     fact.role == RuleRole::Instigator) &&
					    std::none_of(job.context.captured.begin(), job.context.captured.end(),
					                 [&](const auto &capture)
					                 {
						                 return capture.fact == fact;
					                 }))
						throw std::runtime_error("rules: missing saved launch capture");
		}
		pending.push_back(job);
	}
	for (const auto &value : saved.at("results"))
	{
		rule_saved_fields(value, {"id", "result"});
		const auto id = static_cast<std::uint64_t>(rule_saved_integer(value.at("id"), 1));
		rule_saved_text(value.at("result"), 4096, false);
		const auto text = value.at("result").get<std::string>();
		if (id >= job_next || !jobs.insert(id).second || (text != "committed" && !text.starts_with("failed: ")))
			throw std::runtime_error("rules: invalid saved result");
		results.emplace(id, text);
	}
	for (const auto &value : saved.at("active"))
	{
		rule_saved_fields(value,
		                  {"id", "source", "definition", "group_source", "context", "origin", "expiry", "clocks"});
		const auto id = static_cast<std::uint64_t>(rule_saved_integer(value.at("id"), 1));
		for (const auto field : {"source", "definition", "group_source"})
			rule_saved_text(value.at(field), 1024, false);
		rule_saved_integer(value.at("expiry"), 1);
		validate_saved_rule_context(value.at("origin"));
		Json native = value.at("origin");
		native["hit"] = value.at("context");
		validate_saved_rule_context(native);
		if (!value.at("clocks").is_object() || value.at("clocks").size() > rule_instance_limit - clocks.size())
			throw std::runtime_error("rules: invalid active clock map");
		for (const auto &[key, state] : value.at("clocks").items())
		{
			validate_saved_rule_state(state, now);
			if (state.at("key") != key || !clocks.insert(key).second)
				throw std::runtime_error("rules: duplicate active clock");
		}
		const auto effect = value.get<ActiveRuleEffect>();
		if (id >= instance_next || !instances.insert(id).second || !periodic_templates.contains(effect.definition) ||
		    !same_payload_context(effect.context, effect.origin.hit) || !payload_target(effect.context))
			throw std::runtime_error("rules: invalid saved active host");
		if (effect.expiry > now + periodic_templates.at(effect.definition).duration_ticks)
			throw std::runtime_error("rules: saved active expiry exceeds its native duration frontier");
		active.push_back(effect);
	}
	for (const auto &value : saved.at("ships"))
	{
		rule_saved_fields(value, {"entity", "state"});
		const auto id = static_cast<EntityId>(rule_saved_integer(value.at("entity"), 1));
		if (!entities.contains(id) || runtimes.contains(id))
			throw std::runtime_error("rules: invalid saved clock owner");
		const auto &state = value.at("state");
		rule_saved_fields(state, {"next_generation", "executions", "regions", "membership_initialised"});
		const auto next = rule_saved_integer(state.at("next_generation"), 1);
		rule_saved_bool(state.at("membership_initialised"));
		if (!state.at("executions").is_object() || !state.at("regions").is_array())
			throw std::runtime_error("rules: invalid saved native runtime");
		if (state.at("executions").size() > rule_instance_limit - clocks.size() ||
		    state.at("regions").size() > region_rule_bindings.size())
			throw std::runtime_error("rules: saved native runtime capacity exceeded");
		std::set<std::string> regions;
		for (const auto &region : state.at("regions"))
		{
			rule_saved_text(region, 256, false);
			const auto name = region.get<std::string>();
			if (!region_rule_bindings.contains(name) || !regions.insert(name).second)
				throw std::runtime_error("rules: invalid saved membership");
		}
		if (!state.at("membership_initialised").get<bool>() && !regions.empty())
			throw std::runtime_error("rules: uninitialised membership has regions");
		std::set<std::uint64_t> generations;
		for (const auto &[key, clock] : state.at("executions").items())
		{
			validate_saved_rule_state(clock, now);
			const auto generation = clock.at("generation").get<std::uint64_t>();
			if (clock.at("key") != key || !clocks.insert(key).second ||
			    generation >= static_cast<std::uint64_t>(next) || !generations.insert(generation).second)
				throw std::runtime_error("rules: invalid saved execution generation");
		}
		runtimes.emplace(id, state.get<RuleRuntime>());
	}
	if (clocks.size() > rule_instance_limit)
		throw std::runtime_error("rules: saved execution capacity exceeded");
	for (const auto &value : saved.at("origins"))
	{
		rule_saved_fields(value, {"target", "ordnance", "source", "context"});
		const auto target = static_cast<EntityId>(rule_saved_integer(value.at("target"), 1));
		rule_saved_bool(value.at("ordnance"));
		rule_saved_text(value.at("source"), 1024, false);
		validate_saved_rule_context(value.at("context"));
		const RuleOriginKey key{target, value.at("ordnance").get<bool>(), value.at("source").get<std::string>()};
		if (!origins.emplace(key, value.at("context").get<NativeRuleContext>()).second)
			throw std::runtime_error("rules: duplicate saved native origin");
	}
	// Restore only derived addresses after publishing the saved native owners.
	// World restore operates on an isolated candidate; failures discard it.
	commit_active_rule_effects(active);
	rule_origins = std::move(origins);
	for (auto &[id, runtime] : runtimes)
		entities.at(id).set<RuleRuntime>(std::move(runtime));
	std::map<std::string, NativeRuleSource> sources;
	for (auto &source : lifecycle_rule_sources())
		if (!sources.emplace(source.key, std::move(source)).second)
			throw std::runtime_error("rules: duplicate native source identity");
	// A native hull death may be saved before its first closure phase. Its
	// still-authored officer/duty bindings remain valid identities, while normal
	// source views deliberately withhold their eligibility and contribution work.
	std::map<std::string, NativeRuleSource> retiring_sources;
	for (const auto &[id, ship] : entities)
		if (const auto *death = ship.try_get<Destructible>();
		    death && death->destroyed_tick >= 0 && death->durability == 0 && !death->processed && combat_dead(ship))
		{
			const auto *structure = ship.try_get<CombatStructure>();
			if (structure && std::any_of(structure->compartments.begin(), structure->compartments.end(),
			                             [](const auto &compartment)
			                             {
				                             return compartment.hp != 0;
			                             }))
				continue;
			for (auto &source : rule_sources(ship, true))
				if ((source.kind == RuleOwnerKind::Officer || source.kind == RuleOwnerKind::Equipment) &&
				    !source.active_effect && source.context.owner_effect_kind == NativeEffectKind::None &&
				    source.context.owner_grant.empty())
				{
					source.eligible = false;
					retiring_sources.emplace(source.key, std::move(source));
				}
		}
	const auto validate_link =
	    [&](const std::string &key, const RuleExecutionState &state, std::uint64_t active_id, EntityId host = 0)
	{
		const auto found = sources.find(state.source_key);
		const NativeRuleSource *source = found == sources.end() ? nullptr : &found->second;
		if (!source && !active_id)
			if (const auto retired = retiring_sources.find(state.source_key); retired != retiring_sources.end())
				source = &retired->second;
		if (!source || source->active_effect != active_id || !source->bindings || state.owner_kind != source->kind ||
		    (host && (source->owner.entity ? source->owner.entity : source->context.subject.entity) != host))
			throw std::runtime_error("rules: saved clock has no native source: " + state.source_key + " binding " +
			                         state.binding);
		const auto &bindings = *source->bindings;
		const auto binding = std::find_if(bindings.begin(), bindings.end(),
		                                  [&](const auto &item)
		                                  {
			                                  return item.id == state.binding && item.rule == state.definition;
		                                  });
		if (binding == bindings.end() ||
		    key != rule_key(source->key, binding->id, bind_rule_context(*binding, source->context).subject) ||
		    state.context.owner != source->owner || state.context.owner_grant != source->context.owner_grant ||
		    state.context.owner_effect_kind != source->context.owner_effect_kind ||
		    state.context.owner_effect_id != source->context.owner_effect_id)
			throw std::runtime_error("rules: saved binding/source address mismatch");
	};
	for (const auto &[id, ship] : entities)
		for (const auto &[key, state] : ship.get<RuleRuntime>().executions)
			validate_link(key, state, 0, id);
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		for (const auto &[key, state] : effect.clocks)
			validate_link(key, state, effect.id);
	}
	for (const auto &[key, context] : rule_origins)
		if (!rule_origin_live(key))
			throw std::runtime_error("rules: saved origin has no native lifetime owner");
	std::map<std::string, const RuleExecutionState *> saved_clocks;
	for (const auto &[id, ship] : entities)
		for (const auto &[key, state] : ship.get<RuleRuntime>().executions)
			saved_clocks.emplace(key, &state);
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		for (const auto &[key, state] : effect.clocks)
			saved_clocks.emplace(key, &state);
	}
	const auto validate_maintained_row = [&](EntityId id, const PropertyContribution &contribution)
	{
		const auto found = saved_clocks.find(parent_rule_execution_key(contribution.source));
		bool matching = false;
		if (found != saved_clocks.end())
		{
			const auto &state = *found->second;
			const auto &rule = rule_templates.at(state.definition);
			if (rule.phase == RulePhase::Maintained && state.active)
			{
				EffectGroupInvocation frame;
				frame.rule_context = state.applied_context;
				frame.hit_context = state.applied_context.hit;
				for (const auto &operation : rule.effects.operations)
				{
					if (operation.kind != EffectOperationKind::Apply || operation.effect != contribution.definition ||
					    state.key + "." + operation.grant != contribution.source)
						continue;
					const auto actor = effect_group_actor(frame, operation.target);
					if (actor.entity != id || actor.ordnance ||
					    effect_group_equipment(frame, operation.equipment) != contribution.equipment)
						continue;
					const auto role = operation.role == "$role" ? state.applied_context.property_role : operation.role;
					const auto &definition = modifier_templates.at(contribution.definition);
					matching = std::any_of(definition.contributions.begin(), definition.contributions.end(),
					                       [&](const auto &row)
					                       {
						                       return row.name == contribution.name &&
						                              row.property == contribution.property &&
						                              (role.empty() || row.role == role) &&
						                              (state.applied_context.property_filter.empty() ||
						                               row.property == state.applied_context.property_filter);
					                       });
					if (matching)
						break;
				}
			}
		}
		if (contribution.maintained != matching)
			throw std::runtime_error("rules: maintained property row has no matching saved Apply owner");
	};
	std::set<std::string> maintained_sources;
	for (const auto &[key, source] : sources)
		if (source.bindings)
			for (const auto &binding : *source.bindings)
				if (rule_templates.at(binding.rule).phase == RulePhase::Maintained)
					maintained_sources.insert(
					    rule_key(key, binding.id, bind_rule_context(binding, source.context).subject));
	const auto check_grant_clock = [&](const std::string &source)
	{
		const auto parent = parent_rule_execution_key(source);
		if (maintained_sources.contains(parent) && !clocks.contains(parent))
			throw std::runtime_error("rules: maintained native grant has no saved clock");
	};
	for (const auto &[id, ship] : entities)
	{
		if (const auto properties = property_snapshot(ship))
			for (const auto &contribution : properties->contributions)
			{
				validate_maintained_row(id, contribution);
				check_grant_clock(contribution.source);
			}
		if (const auto *shields = ship.try_get<ShieldComponent>())
			for (const auto &contribution : shields->contributions)
				check_grant_clock(contribution.source);
		if (const auto *abilities = ship.try_get<AbilityRuntime>())
			for (const auto &[key, grant] : abilities->grants)
				check_grant_clock(grant.parent);
	}
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		check_grant_clock(effect.source);
	}
	pending_rules = std::move(pending);
	rule_results = std::move(results);
	next_rule_job = job_next;
	next_rule_instance = instance_next;
	draining_rules = 0;
	clear_rule_upkeep();
	rebuild_rule_clock_addresses();
}
