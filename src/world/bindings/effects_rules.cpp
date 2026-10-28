module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::rule_entity(const RuleActor &actor) const -> flecs::entity
{
	if (!actor.entity)
		return {};
	if (actor.ordnance)
		return payload_target(rule_hit_context(actor));
	const auto found = entities.find(actor.entity);
	return found == entities.end() ? flecs::entity{} : found->second;
}

auto Runtime::rule_actor_available(const RuleActor &actor) const -> bool
{
	const auto entity = rule_entity(actor);
	if (!entity || combat_dead(entity))
		return false;
	if (actor.kind != PayloadOutcomeKind::Entity || actor.ordnance)
		return payload_context_available(rule_hit_context(actor));
	return true;
}

auto Runtime::rule_actor_eligible(const RuleActor &actor) const -> bool
{
	const auto entity = rule_entity(actor);
	if (!entity || combat_dead(entity))
		return false;
	if (actor.ordnance)
		return true;
	if (!actor.officer.empty())
	{
		const auto *personnel = entity.try_get<PersonnelState>();
		if (!personnel)
			return false;
		const auto &definitions = definition(actor.entity).personnel.officers;
		for (std::size_t i = 0; i < definitions.size(); ++i)
			if (definitions[i].id == actor.officer)
				return officer_healthy(personnel->officers.at(i)) && personnel->officers.at(i).present &&
				       !personnel->officers.at(i).duty.empty() &&
				       away_officer_location(actor.entity, actor.officer).empty();
		return false;
	}
	if (!actor.equipment.empty() && !actor.equipment.starts_with("effect:") && !actor.equipment.starts_with("away:"))
	{
		const auto fitted = instance(entity, actor.equipment);
		return fitted && fitted->enabled && !structurally_disabled(entity, actor.equipment);
	}
	return !departed(actor.entity);
}

auto Runtime::rule_actor_enabled(const RuleActor &actor) const -> bool
{
	const auto entity = rule_entity(actor);
	if (!entity || combat_dead(entity))
		return false;
	if (actor.ordnance)
		return true;
	if (!actor.equipment.empty())
	{
		if (!entity.has<EquipmentIndex>())
			return false;
		const auto fitted = instance(entity, actor.equipment);
		return fitted && fitted->enabled;
	}
	return rule_actor_eligible(actor);
}

auto Runtime::rule_native_output(const RuleActor &actor) const -> std::optional<std::int64_t>
{
	const auto entity = rule_entity(actor);
	if (!entity)
		return {};
	if (!rule_actor_eligible(actor))
		return 0;
	if (actor.kind == PayloadOutcomeKind::Shield)
		if (const auto *shields = entity.try_get<ShieldComponent>())
			for (const auto &contribution : shields->contributions)
				if (contribution.effect_id == actor.contribution &&
				    contribution.definition.name == actor.contribution_name)
					return rule_source_output(actor.entity, contribution.source);
	if (!actor.equipment.empty())
	{
		if (!entity.has<EquipmentIndex>())
			return {};
		const auto fitted = instance(entity, actor.equipment);
		if (!fitted)
			return {};
		if (const auto *power = entity.try_get<ShipResources>(); power && fitted->entity.has<ResourceBinding>())
		{
			const auto slot = fitted->entity.get<ResourceBinding>().slot;
			const auto &grant = power->grants.at(slot);
			const auto &request = power->requests.at(slot);
			return grant.blocked || (!request.duty_permille && !request.extra_rate && !request.discrete)
			           ? 0
			           : grant.supply_permille;
		}
	}
	return 1000;
}

auto Runtime::rule_native_property(flecs::entity entity, const RuleActor &actor, const std::string &property,
                                   bool base_view) const -> std::optional<std::int64_t>
{
	if (actor.kind == PayloadOutcomeKind::Shield)
	{
		if (const auto *shields = entity.try_get<ShieldComponent>())
			for (const auto &contribution : shields->contributions)
				if (contribution.effect_id == actor.contribution &&
				    contribution.definition.name == actor.contribution_name)
				{
					const auto &native = contribution.definition.shield;
					if (property == "recharge")
						return base_view ? native.recharge_rate * 1000
						                 : shield_property(entity, contribution, property, native.recharge_rate * 1000,
						                                   native.recharge_modifier);
					if (property == "off_recharge")
						return base_view ? native.off_recharge_rate * 1000
						                 : shield_property(entity, contribution, property,
						                                   native.off_recharge_rate * 1000, native.recharge_modifier);
					if (property == "sustain")
						return base_view ? native.sustain_energy
						                 : shield_property(entity, contribution, property, native.sustain_energy,
						                                   native.sustain_modifier);
					if (property == "discharge")
						return base_view ? native.discharge_rate
						                 : shield_property(entity, contribution, property, native.discharge_rate,
						                                   native.discharge_modifier);
					if (property == "protection")
						return base_view
						           ? 1000
						           : shield_property(entity, contribution, property, 1000, native.protection_modifier);
					return {};
				}
		return {};
	}
	if (!entity.has<EquipmentIndex>())
		return {};
	const auto fitted = instance(entity, actor.equipment);
	if (!fitted)
		return {};
	std::int64_t base = 1000;
	if (const auto *flight = fitted->entity.try_get<FlightRuntime>())
	{
		if (property == "speed")
			base = flight->definition.maximum_speed;
		else if (property == "propulsion")
			base = flight->definition.acceleration;
		else if (property == "turn")
			base = flight->definition.turn_rate;
	}
	if (const auto *directed = directed_mode(actor.entity, actor.equipment))
		if (property == "sensors")
			base = directed->range;
	if (const auto *engine = fitted->entity.try_get<Engine>())
	{
		if (property == "speed")
			base = engine->max_speed;
		else if (property == "propulsion")
			base = engine->acceleration;
	}
	if (const auto *steering = fitted->entity.try_get<Steering>(); steering && property == "turn")
		base = steering->rate;
	if (const auto *radar = fitted->entity.try_get<Radar>();
	    radar && property == "sensors" && !directed_mode(actor.entity, actor.equipment))
		base = radar->range;
	if (property == "comms")
		base = fitted->maximum;
	if (const auto *mechanism = fitted->entity.try_get<CombatMechanism>())
	{
		if (mechanism->definition.kind == OperatingSystem::Shield)
		{
			const auto &shield = mechanism->definition.get<ShieldDefinition>();
			if (property == "recharge")
				base = shield.recharge_rate * 1000;
			else if (property == "off_recharge")
				base = shield.off_recharge_rate * 1000;
			else if (property == "sustain")
				base = shield.sustain_energy;
			else if (property == "discharge")
				base = shield.discharge_rate;
		}
		if (mechanism->definition.kind == OperatingSystem::Turret && property == "traverse")
			base = mechanism->definition.get<TurretDefinition>().traverse;
		if (mechanism->definition.kind == OperatingSystem::Team && property == "repair")
			base = mechanism->definition.get<TeamDefinition>().repair_rate;
	}
	if (property == "processing")
	{
		base = 0;
		const auto *processors = entity.try_get<ShipProcessors>();
		if (!processors)
			return {};
		bool found = false;
		for (const auto &[id, processor] : processors->values)
			if (processor.definition().equipment == actor.equipment)
			{
				const auto rate = processor.definition().work_per_second;
				if (rate > resource_limit - base)
					return {};
				base += rate;
				found = true;
			}
		if (!found)
			return {};
	}
	if (!property_compatible(property, fitted->kind))
	{
		const auto *directed = directed_definition(actor.entity, actor.equipment);
		if (!directed || !property_compatible(property, fitted->kind, *directed))
			return {};
	}
	return base_view ? base : effective_property(entity, actor.equipment, property, base);
}

auto Runtime::read_rule_fact(const RuleFactBinding &fact, const NativeRuleContext &context) const -> state_tree::Value
{
	using Value = state_tree::Value;
	for (const auto &capture : context.captured)
		if (capture.fact == fact && (fact.role != RuleRole::Owner || context.capture_owner))
			return capture.value;
	const auto actor = rule_actor(context, fact.role);
	const auto entity = rule_entity(actor);
	const bool virtual_region =
	    !context.region.empty() && !actor.entity && (fact.role == RuleRole::Owner || fact.role == RuleRole::Source);
	switch (fact.kind)
	{
	case RuleFactKind::Tick:
		return Value::integer(context.tick);
	case RuleFactKind::Elapsed:
		return Value::integer(context.elapsed);
	case RuleFactKind::PresentedDamage:
		return Value::integer(context.presented);
	case RuleFactKind::ModifiedDamage:
		return Value::integer(context.modified);
	case RuleFactKind::NegatedDamage:
		return Value::integer(context.negated);
	case RuleFactKind::AbsorbedDamage:
		return Value::integer(context.absorbed);
	case RuleFactKind::ResidualDamage:
		return Value::integer(context.residual);
	case RuleFactKind::Strength:
		return Value::integer(context.strength);
	case RuleFactKind::InflictedDamage:
		return Value::integer(context.inflicted);
	case RuleFactKind::Output:
		if (virtual_region)
			return Value::integer(1000);
		if (const auto output = rule_native_output(actor))
			return Value::integer(*output);
		return {};
	case RuleFactKind::Entity:
		return entity ? Value::reference(state_tree::Type::Entity, actor.entity) : Value{};
	case RuleFactKind::Exists:
		return Value::boolean(virtual_region || bool(entity));
	case RuleFactKind::Enabled:
		return Value::boolean(virtual_region || rule_actor_enabled(actor));
	case RuleFactKind::Eligible:
		return Value::boolean(virtual_region || rule_actor_eligible(actor));
	case RuleFactKind::Heat:
		if (entity && entity.has<EquipmentIndex>() && !actor.equipment.empty())
			if (const auto fitted = instance(entity, actor.equipment); fitted && fitted->entity.has<ResourceBinding>())
				if (const auto *power = entity.try_get<ShipResources>())
				{
					const auto store = power->definition.systems.at(fitted->entity.get<ResourceBinding>().slot).store;
					if (store < power->network.stores().size())
						return Value::integer(power->network.stores()[store].heat);
				}
		return {};
	case RuleFactKind::ResourceQuantity:
		if (entity)
			try
			{
				return Value::integer(named_stock(entity, fact.key).stock.quantity(fact.aux));
			}
			catch (const std::exception &)
			{
				return {};
			}
		return {};
	case RuleFactKind::CompletedWork:
	{
		const auto ability = ability_completed_work(actor, fact.key);
		const auto process = process_completed_work(actor, fact.key);
		if (ability && process && *ability <= resource_limit - *process)
			return Value::integer(*ability + *process);
		return {};
	}
	case RuleFactKind::ProcessWork:
	case RuleFactKind::ProcessActivity:
		return {}; // The process quote freezes these typed facts before evaluation.
	case RuleFactKind::OfficerPresent:
	case RuleFactKind::OfficerHealthy:
	case RuleFactKind::OfficerOnDuty:
	{
		const auto *personnel = entity ? entity.try_get<PersonnelState>() : nullptr;
		if (!personnel || actor.officer.empty())
			return {};
		const auto &officers = definition(actor.entity).personnel.officers;
		for (std::size_t i = 0; i < officers.size(); ++i)
			if (officers[i].id == actor.officer)
			{
				const auto &state = personnel->officers.at(i);
				return Value::boolean(fact.kind == RuleFactKind::OfficerPresent   ? state.present
				                      : fact.kind == RuleFactKind::OfficerHealthy ? officer_healthy(state)
				                                                                  : !state.duty.empty());
			}
		return {};
	}
	case RuleFactKind::PropertyBefore:
	case RuleFactKind::PropertyAfter:
		return {}; // Frozen by the successful transaction which emitted the event.
	case RuleFactKind::Property:
		if (property_catalogue->contains(fact.key))
			return read_custom_property(actor, fact.key, fact.aux == "base").value_or(Value{});
		if (entity)
			if (const auto value = rule_native_property(entity, actor, fact.key, fact.aux == "base"))
				return Value::integer(*value);
		return {};
	}
	return {};
}

auto Runtime::freeze_rule_context(const RuleDefinition &rule, const NativeRuleContext &context) const -> RuleContext
{
	RuleContext frozen;
	frozen.facts.reserve(rule.facts.size());
	for (const auto &fact : rule.facts)
		frozen.facts.push_back(read_rule_fact(fact, context));
	return frozen;
}

auto Runtime::rule_binding_matches(const RuleBinding &binding, const NativeRuleContext &context) const -> bool
{
	const auto actor = rule_actor(context, binding.target);
	const auto entity = rule_entity(actor);
	if (!entity)
		return false;
	for (const auto &tag : binding.tags)
	{
		if (actor.ordnance)
		{
			if (tag != "ordnance" && tag != "missile")
				return false;
		}
		else
		{
			const auto &tags = definition(actor.entity).tags;
			if (std::find(tags.begin(), tags.end(), tag) == tags.end())
				return false;
		}
	}
	return true;
}

auto Runtime::enqueue_rule(const std::string &rule, const std::string &source, const NativeRuleContext &context,
                           RuleEventPhase phase, std::int64_t due) -> bool
{
	return rule_events.enqueue(rule, source, context, phase, due);
}

auto Runtime::prepared_damage_failure(const PreparedEffectGroup &quote) const -> std::string
{
	if (!rule_events.can_schedule_damage(quote.damages.size()))
		return "Scheduled damage capacity exhausted";
	for (const auto &damage : quote.damages)
	{
		const auto target = damage.outcome
		                        ? payload_target(damage.context)
		                        : (entities.contains(damage.target) ? entities.at(damage.target) : flecs::entity{});
		if (!target || damage.amount <= 0 || combat_dead(target))
			return "Scheduled damage target unavailable";
	}
	return {};
}

auto Runtime::queue_prepared_damage(const PreparedEffectGroup &quote) -> void
{
	for (const auto &damage : quote.damages)
	{
		const auto &frame = quote.invocations.at(damage.invocation);
		auto context = effect_invocation_context(frame, quote.tick);
		context.hit = damage.outcome ? damage.context : PayloadHitContext{damage.target};
		context.subject = rule_hit_actor(context.hit);
		context.target = context.subject;
		rule_events.schedule_damage({0,
		                             static_cast<std::int64_t>(ecs.get<Clock>().tick) + 1,
		                             RuleEventPhase::Payload,
		                             {},
		                             frame.source,
		                             damage.amount,
		                             std::move(context)});
	}
}

auto Runtime::execute_rule_quote(const RuleEvaluation &evaluation, const NativeRuleContext &context,
                                 const std::string &source, bool maintained, std::int64_t output) -> std::string
{
	if (evaluation.status == RuleEvaluationStatus::Unmatched)
		return {};
	if (evaluation.status != RuleEvaluationStatus::Ready)
		return "Invalid rule arithmetic or missing fact";
	return execute_effect_group(evaluation.quote.effects, context.subject.entity, context.instigator.entity, source,
	                            &context.hit, context.source.equipment, &context, output, maintained);
}

auto Runtime::step_rule_events(RuleEventPhase phase) -> void
{
	RuleEventCallbacks callbacks;
	callbacks.freeze = [&](const PendingRuleExecution &job)
	{
		return evaluate_rule(rule_templates.at(job.rule),
		                     freeze_rule_context(rule_templates.at(job.rule), job.context));
	};
	callbacks.execute = [&](const PendingRuleExecution &job, const RuleEvaluation &frozen) -> std::string
	{
		if (job.rule.empty())
		{
			const auto target = payload_target(job.context.hit);
			if (!target || !payload_context_available(job.context.hit))
				return "failed: target unavailable";
			apply_payload_damage(target, job.damage, job.context.hit, &job.context);
			return "committed";
		}
		const auto failure = execute_rule_quote(frozen, job.context, job.source_key);
		return failure.empty() ? "committed" : "failed: " + failure;
	};
	// Retire lost native owners after the outer batch has published its state.
	callbacks.settled = [&]
	{
		refresh_rule_eligibility();
	};
	rule_events.drain(phase, static_cast<std::int64_t>(ecs.get<Clock>().tick), callbacks);
}
} // namespace phoenix::world_detail
