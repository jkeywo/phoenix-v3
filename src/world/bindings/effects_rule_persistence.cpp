module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::rule_continuation_validator() const -> RuleContinuationValidator
{
	RuleValidationPorts ports;
	ports.actor_identity = [this](const RuleActor &actor)
	{
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
	};
	ports.physical_exists = [this](EntityId id)
	{
		return entities.contains(id);
	};
	ports.quantity_compatible = [this](const CustomPropertyAddress &address, const std::string &property)
	{
		const auto *owner = custom_property_state(address);
		return owner && owner->bases.contains(property);
	};
	ports.stock_accepts = [this](EntityId id, const std::string &store, const std::string &resource)
	{
		return named_stock(entities.at(id), store).stock.accepts(resource);
	};
	return RuleContinuationValidator({rule_templates, *property_catalogue, *resource_catalogue, region_rule_bindings},
	                                 std::move(ports), static_cast<std::int64_t>(ecs.get<Clock>().tick));
}

auto Runtime::rule_saved_fields(const Json &value, std::initializer_list<const char *> fields) -> void
{
	if (!value.is_object() || value.size() != fields.size())
		throw std::runtime_error("rules: invalid saved object layout");
	for (const auto field : fields)
		if (!value.contains(field))
			throw std::runtime_error("rules: missing saved field");
}

auto Runtime::rule_saved_integer(const Json &value, std::int64_t minimum, std::int64_t maximum) -> std::int64_t
{
	return checked_integer(value, minimum, maximum, "rules.saved");
}

auto Runtime::rule_saved_text(const Json &value, std::size_t maximum, bool empty) -> void
{
	if (!value.is_string() || value.get_ref<const std::string &>().size() > maximum ||
	    (!empty && value.get_ref<const std::string &>().empty()))
		throw std::runtime_error("rules: invalid saved identity");
}

auto Runtime::rule_saved_bool(const Json &value) -> void
{
	if (!value.is_boolean())
		throw std::runtime_error("rules: invalid saved boolean");
}

auto Runtime::saved_rule_key(const std::string &value) -> bool
{
	return value.size() == 69 && value.starts_with("rule.") &&
	       std::all_of(value.begin() + 5, value.end(),
	                   [](char c)
	                   {
		                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	                   });
}

auto Runtime::validate_saved_rule_actor(const Json &value) const -> void
{
	return rule_continuation_validator().actor(value);
}

auto Runtime::validate_saved_rule_context(const Json &value) const -> void
{
	return rule_continuation_validator().context(value);
}

auto Runtime::validate_saved_work_credit(const Json &credit) const -> void
{
	return rule_continuation_validator().credit(credit);
}

auto Runtime::validate_saved_rule_state(const Json &value, std::int64_t now) const -> void
{
	return rule_continuation_validator().state(value, now);
}

auto Runtime::save_rules() const -> Json
{
	Json out = {{"schema", 1},
	            {"next_job", rule_events.next_identity()},
	            {"pending", rule_events.pending()},
	            {"results", Json::array()},
	            {"ships", Json::array()},
	            {"origins", Json::array()}};
	for (const auto &[id, result] : rule_events.results())
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

auto Runtime::restore_rules(const Json &saved, const Json &conditions, const Json &next_condition) -> void
{
	rule_saved_fields(saved, {"schema", "next_job", "pending", "results", "ships", "origins"});
	if (rule_saved_integer(saved.at("schema"), 1, 1) != 1)
		throw std::runtime_error("rules: unsupported save schema");
	const auto job_next = static_cast<std::uint64_t>(rule_saved_integer(saved.at("next_job"), 1));
	const auto instance_next = static_cast<std::uint64_t>(rule_saved_integer(next_condition, 1));
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto field : {"pending", "results", "ships", "origins"})
		if (!saved.at(field).is_array())
			throw std::runtime_error("rules: saved roster is not an array");
	if (!conditions.is_array())
		throw std::runtime_error("rules: saved condition roster is not an array");
	if (saved.at("pending").size() > rule_pending_limit || saved.at("results").size() > 1024 ||
	    conditions.size() > rule_instance_limit || saved.at("ships").size() != entities.size() ||
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
			        ? trigger == RuleTrigger::Tick || trigger == RuleTrigger::Occupancy ||
			              trigger == RuleTrigger::PropertyChanged
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
	for (const auto &value : conditions)
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
	const auto validate_maintained_row =
	    [&](EntityId id, const ModifierApplication &contribution, const PropertyValue &value)
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
					    rule_operation_equipment(state.applied_context, operation) != contribution.equipment)
						continue;
					const auto role = operation.role == "$role" ? state.applied_context.property_role : operation.role;
					const auto &definition = modifier_templates.at(contribution.definition);
					matching =
					    std::any_of(definition.contributions.begin(), definition.contributions.end(),
					                [&](const auto &row)
					                {
						                return row.name == value.name &&
						                       (value.custom.empty() ? property_key(row.property) == value.property
						                                             : row.property == value.custom) &&
						                       (role.empty() || row.role == role) &&
						                       (state.applied_context.property_filter.empty() ||
						                        row.property == state.applied_context.property_filter);
					                });
					if (matching)
						break;
				}
			}
		}
		if (contribution.modifier.maintained != matching)
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
		for (const auto &application : read_property_effects(ship).applications)
		{
			for (const auto &value : application.modifier.values)
				validate_maintained_row(id, application, value);
			check_grant_clock(application.source);
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
	rule_events.restore(std::move(pending), std::move(results), job_next);
	active_rules.set_next_identity(instance_next);
	clear_rule_upkeep();
	rebuild_rule_clock_addresses();
}
} // namespace phoenix::world_detail
