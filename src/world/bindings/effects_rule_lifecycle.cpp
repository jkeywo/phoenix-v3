module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::rule_clock_storage() -> RuleClockStorage
{
	RuleClockStorage storage;
	storage.runtime = [this](EntityId id, bool create) -> RuleRuntime *
	{
		const auto owner = entities.find(id);
		if (owner == entities.end())
			return nullptr;
		if (create && !owner->second.has<RuleRuntime>())
			owner->second.set<RuleRuntime>({});
		return create ? &owner->second.get_mut<RuleRuntime>() : owner->second.try_get_mut<RuleRuntime>();
	};
	storage.active = [this](std::uint64_t id) -> ActiveRuleEffect *
	{
		const auto entity = active_rule_entity(id);
		return entity ? &entity.get_mut<ActiveRuleEffect>() : nullptr;
	};
	return storage;
}

auto Runtime::rule_clock_read_storage() const -> RuleClockReadStorage
{
	RuleClockReadStorage storage;
	storage.runtime = [this](EntityId id) -> const RuleRuntime *
	{
		const auto owner = entities.find(id);
		return owner == entities.end() ? nullptr : owner->second.try_get<RuleRuntime>();
	};
	storage.active = [this](std::uint64_t id) -> const ActiveRuleEffect *
	{
		const auto entity = active_rule_entity(id);
		return entity ? &entity.get<ActiveRuleEffect>() : nullptr;
	};
	return storage;
}

auto Runtime::rebuild_rule_clock_addresses() -> void
{
	rule_clocks.begin_rebuild();
	for (const auto &[id, ship] : entities)
		if (const auto *runtime = ship.try_get<RuleRuntime>())
			rule_clocks.index_runtime(id, *runtime);
	for (auto entity : active_rule_rows())
		rule_clocks.index_active(entity.get<ActiveRuleEffect>());
	rule_clocks.finish_rebuild();
}

auto Runtime::find_rule_execution(const std::string &key) -> RuleExecutionState *
{
	return rule_clocks.find(key, rule_clock_storage());
}

auto Runtime::readonly_rule_execution(const std::string &key) const -> const RuleExecutionState *
{
	return rule_clocks.read(key, rule_clock_read_storage());
}

auto Runtime::ensure_rule_execution(const NativeRuleSource &source, const RuleBinding &binding,
                                    const NativeRuleContext &context, bool create) -> RuleExecutionState *
{
	return rule_clocks.ensure(source, binding, context, create, rule_templates, periodic_templates,
	                          rule_clock_storage());
}

auto Runtime::erase_rule_execution(const std::string &key) -> void
{
	erase_custom_property_rate_source(key);
	rule_clocks.erase(key, rule_clock_storage());
}

auto Runtime::rule_clock_capacity(const std::vector<ActiveRuleEffect> *prepared) const -> std::size_t
{
	RuleClockSources sources;
	sources.runtimes = [this](const std::function<void(const RuleRuntime &)> &visit)
	{
		for (const auto &[id, ship] : entities)
			if (const auto *runtime = ship.try_get<RuleRuntime>())
				visit(*runtime);
	};
	sources.active = [this](const std::function<void(const ActiveRuleEffect &)> &visit)
	{
		for (auto entity : active_rule_rows())
			visit(entity.get<ActiveRuleEffect>());
	};
	return rule_clocks.capacity(sources, prepared, periodic_templates);
}

auto Runtime::lifecycle_rule_sources() const -> std::vector<NativeRuleSource>
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

auto Runtime::parent_rule_execution_key(const std::string &source) const -> std::string
{
	if (!source.starts_with("rule."))
		return {};
	const auto separator = source.find('.', 5);
	return separator == std::string::npos ? source : source.substr(0, separator);
}

auto Runtime::rule_grant_available(EntityId, const std::string &source, RuleSourceAvailabilityCache *scratch) const
    -> bool
{
	const auto key = parent_rule_execution_key(source);
	const auto *state = readonly_rule_execution(key);
	if (!state || !rule_templates.contains(state->definition) ||
	    rule_templates.at(state->definition).phase != RulePhase::Maintained)
		return true;
	if (!state->eligible || !state->active || state->stopped || !state->output)
		return false;
	const auto address = rule_clocks.address(key);
	if (!address)
		return false;
	if (address->active)
	{
		const auto entity = active_rule_entity(address->active);
		return entity && entity.get<ActiveRuleEffect>().expiry > static_cast<std::int64_t>(ecs.get<Clock>().tick) &&
		       active_rule_target_available(entity.get<ActiveRuleEffect>());
	}
	const auto host = entities.find(address->ship);
	if (host == entities.end())
		return false;
	RuleSourceAvailabilityCache local;
	auto &cache = scratch ? *scratch : local;
	auto found = cache.find(address->ship);
	if (found == cache.end())
	{
		std::map<std::string, bool> availability;
		for (const auto &view : rule_sources(host->second))
			availability.emplace(view.key, view.eligible);
		found = cache.emplace(address->ship, std::move(availability)).first;
	}
	const auto source_state = found->second.find(state->source_key);
	return source_state != found->second.end() && source_state->second;
}

auto Runtime::rule_lease_available(const std::string &source) const -> bool
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

auto Runtime::rule_source_output(EntityId ship, const std::string &source) const -> std::int64_t
{
	const auto *state = readonly_rule_execution(parent_rule_execution_key(source));
	if (!state || !rule_templates.contains(state->definition) ||
	    rule_templates.at(state->definition).phase != RulePhase::Maintained)
		return 1000;
	return rule_grant_available(ship, source) ? state->output : 0;
}

auto Runtime::capture_rule_custom_targets(const RuleDefinition &rule, const NativeRuleContext &context) -> void
{
	for (const auto &operation : rule.effects.operations)
		if (operation.kind == EffectOperationKind::Apply && modifier_templates.contains(operation.effect))
		{
			const auto actor = rule_actor(context, operation.target);
			capture_custom_property_owner(custom_property_address(
			    custom_property_actor(actor.entity, rule_operation_equipment(context, operation))));
		}
}

auto Runtime::maintained_modifier_exists(const RuleExecutionState &state, const EffectOperation &operation) const
    -> bool
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

auto Runtime::withdraw_rule_grants(RuleExecutionState &state) -> void
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
		effect_transactions.invalidate();
	}
	state.active = false;
	state.output = 0;
	state.applied_context = {};
}

auto Runtime::retire_rule_state(RuleExecutionState &state, bool deactivate) -> void
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

auto Runtime::discover_active_rule_clocks() -> void
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

auto Runtime::retire_active_rule_effects() -> void
{
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	ActiveRuleRetirementObserver retirement;
	retirement.withdraw = [this](RuleExecutionState &state)
	{
		retire_rule_state(state, true);
	};
	retirement.transaction_changed = [this]
	{
		effect_transactions.invalidate();
	};
	active_rules.retire_expired(now, active_rule_target_validity(), rule_clocks, active_rule_mutation_observer(),
	                            retirement);
	std::erase_if(rule_origins,
	              [&](const auto &entry)
	              {
		              if (rule_origin_live(entry.first))
			              return false;
		              mark_custom_property_rates_dirty();
		              return true;
	              });
}

auto Runtime::prepare_rule_sources() -> void
{
	clear_rule_upkeep();
	rebuild_rule_clock_addresses();
	retire_active_rule_effects();
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto sources = lifecycle_rule_sources();
	prune_custom_property_rates(sources);
	auto prepared_frames = rule_clocks.freeze_sources(
	    sources, rule_templates, periodic_templates, rule_clock_storage(), rule_applications(),
	    [this](const RuleBinding &binding, const NativeRuleContext &context)
	    {
		    return rule_binding_matches(binding, context);
	    },
	    now);
	auto &seen = prepared_frames.seen;
	auto &frames = prepared_frames.frames;
	// Every source evaluation above observes the same pre-commit phase.
	std::vector<std::string> retired;
	for (const auto &[key, address] : rule_clocks.addresses())
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

auto Runtime::refresh_rule_eligibility() -> void
{
	// Retirement can remove the native owner of another maintained source. Close
	// that dependency chain without evaluating, activating or paying any rule.
	for (std::size_t pass = 0; pass <= rule_instance_limit; ++pass)
	{
		rebuild_rule_clock_addresses();
		const auto before_epoch = effect_transactions.epoch();
		const auto before_clocks = rule_clocks.size();
		const auto before_active = active_rules.size();
		retire_active_rule_effects();
		std::map<std::string, bool> available;
		const auto sources = lifecycle_rule_sources();
		prune_custom_property_rates(sources);
		for (const auto &source : sources)
			available.emplace(source.key, source.eligible);
		std::vector<std::string> keys;
		for (const auto &[key, address] : rule_clocks.addresses())
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
		if (before_epoch == effect_transactions.epoch() && before_clocks == rule_clocks.size() &&
		    before_active == active_rules.size())
			return;
	}
	throw std::runtime_error("rules: native source retirement did not converge");
}

auto Runtime::step_rule_clocks() -> void
{
	refresh_rule_eligibility();
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto due = rule_clocks.advance_due(
	    lifecycle_rule_sources(), rule_templates, periodic_templates, rule_clock_storage(),
	    [this](const RuleBinding &binding, const NativeRuleContext &context)
	    {
		    return rule_binding_matches(binding, context);
	    },
	    now);
	// Every clock frontier is advanced before the common phase queue is drained.
	for (const auto &event : due)
		enqueue_rule(event.rule, event.source_key, event.context, event.phase, event.due);
}
} // namespace phoenix::world_detail
