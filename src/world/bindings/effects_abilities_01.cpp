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
auto Runtime::ability_source_exists(EntityId ship, const std::string &source, const std::string &owner) const -> bool
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

auto Runtime::ability_execution(EntityId ship, const std::string &grant) const -> const AbilityGrantState *
{
	if (!entities.contains(ship))
		return nullptr;
	const auto *state = entities.at(ship).try_get<AbilityRuntime>();
	if (!state)
		return nullptr;
	const auto at = state->grants.find(grant);
	return at == state->grants.end() ? nullptr : &at->second;
}

auto Runtime::ability_definition(EntityId ship, const std::string &grant) const -> const AbilityDefinition &
{
	const auto *execution = ability_execution(ship, grant);
	if (!execution)
		throw std::runtime_error("Ability grant unavailable");
	return definition(ship).abilities.catalog->definitions.at(execution->definition);
}

auto Runtime::ability_recovery_until(EntityId ship, const AbilityGrantState &g) const -> std::uint64_t
{
	return phoenix::ability_recovery_until(entities.at(ship).get<AbilityRuntime>(), g, ability_definition(ship, g));
}

auto Runtime::ability_definition(EntityId ship, const AbilityGrantState &grant) const -> const AbilityDefinition &
{
	return definition(ship).abilities.catalog->definitions.at(grant.definition);
}

auto Runtime::ability_control_owner(EntityId ship, const AbilityControlDefinition &c) const -> std::string
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

auto Runtime::resolve_ability_control(EntityId ship, const AbilityGrantState &g) const
    -> const AbilityControlDefinition *
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

auto Runtime::ability_target_valid(EntityId ship, const AbilityControlDefinition &c, const AbilityDefinition &d,
                                   EntityId target, ConsoleContacts &cache) const -> bool
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

auto Runtime::ability_blocker(EntityId ship, const AbilityControlDefinition &c, const AbilityGrantState &g,
                              EntityId target, ConsoleContacts &cache) const -> std::string
{
	if (!ability_source_exists(ship, g.source, g.owner) || departed(ship) || combat_dead(entities.at(ship)))
		return "Ability source unavailable";
	if (!g.parent.empty() && !rule_grant_available(ship, g.parent))
		return "Ability grant source unavailable";
	const auto &d = definition(ship).abilities.catalog->definitions.at(g.definition);
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
	if (d.requirements)
	{
		NativeRuleContext context;
		context.owner = {ship};
		if (g.source == "officer")
			context.owner.officer = g.owner;
		else
			context.owner.equipment = g.owner;
		context.source = context.instigator = context.owner;
		context.subject = {ship};
		context.target = {target ? target : ship};
		context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
		state_tree::Value value;
		if (!evaluate_rule_expression(*d.requirements->condition, freeze_rule_context(*d.requirements, context),
		                              value) ||
		    value.type != state_tree::Type::Boolean || !value.number)
			return "Ability requirements unavailable";
	}
	const bool continuing = g.phase == "active" || (g.phase == "paused" && g.resume_phase == "active");
	if ((!continuing && !ability_target_valid(ship, c, d, target, cache)) ||
	    (continuing && target != ship &&
	     (!entities.contains(target) || departed(target) || combat_dead(entities.at(target)))))
		return "Target unavailable";
	return {};
}

auto Runtime::ability_apparatus_blocker(EntityId ship, const AbilityGrantState &g) const -> std::string
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

auto Runtime::ability_conflict(EntityId ship, const AbilityGrantState &g) const -> std::string
{
	const auto &definitions = definition(ship).abilities.catalog->definitions;
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
	return process_ability_conflict(ship, g);
}

auto Runtime::initialise_abilities(flecs::entity ship) -> void
{
	std::vector<AbilityGrantSeed> seeds;
	const auto id = ship.get<StableId>().id;
	for (const auto &officer : definition(id).personnel.officers)
		for (const auto &ability : officer.abilities)
			seeds.push_back({"officer", officer.id, ability});
	for (const auto &[owner, abilities] : definition(id).abilities.equipment_grants)
		for (const auto &ability : abilities)
			seeds.push_back({"equipment", owner, ability});
	ship.set<AbilityRuntime>(initial_ability_runtime(seeds));
}

auto Runtime::ability_begin_recovery(EntityId ship, AbilityRuntime &runtime, const AbilityGrantState &g) const -> void
{
	phoenix::ability_begin_recovery(runtime, g, ability_definition(ship, g), ecs.get<Clock>().tick);
}

auto Runtime::ability_recovery_reservations(EntityId ship, const AbilityGrantState &g) const -> std::vector<std::string>
{
	return phoenix::ability_recovery_reservations(g, ability_definition(ship, g));
}

auto Runtime::ability_recovery_capacity(EntityId ship, const AbilityGrantState &g) const -> bool
{
	return phoenix::ability_recovery_capacity(entities.at(ship).get<AbilityRuntime>(), g, ability_definition(ship, g),
	                                          ecs.get<Clock>().tick);
}

auto Runtime::stop_ability(EntityId ship, AbilityRuntime &runtime, AbilityGrantState &g) -> void
{
	if (!ability_running(g))
		return;
	const auto &d = definition(ship).abilities.catalog->definitions.at(g.definition);
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

auto Runtime::reset_ability_endpoint(EntityId ship, const AbilityControlDefinition &c) -> void
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

auto Runtime::stop_abilities_for_duty(EntityId ship, const std::string &duty) -> void
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
				g.retained_request = false;
			}
	for (const auto &c : definition(ship).abilities.controls)
		if (c.source == "duty" && c.owner == duty)
			reset_ability_endpoint(ship, c);
	sync_psychic_contributions();
}

auto Runtime::clear_retired_ability_control(EntityId ship, const AbilityGrantState &removed,
                                            const AbilityRuntime &remaining) -> void
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

auto Runtime::prepare_ability_grant(AbilityRuntime &runtime, EntityId ship, const EffectOperation &operation,
                                    const std::string &parent) -> void
{
	const auto source = operation.officer.empty() ? "equipment" : "officer";
	const auto owner = operation.officer.empty() ? operation.equipment : operation.officer;
	if (!ability_source_exists(ship, source, owner))
		throw std::runtime_error("Ability grant source unavailable");
	const auto id = "temporary:" + parent + ":" + source + ":" + owner;
	const auto old = runtime.grants.find(id);
	if (operation.kind == EffectOperationKind::Remove)
	{
		if (old != runtime.grants.end())
		{
			stop_ability(ship, runtime, old->second);
			runtime.grants.erase(old);
		}
		return;
	}
	const auto &definition = ability_grant_templates.at(operation.effect);
	if (!this->definition(ship).abilities.catalog->definitions.contains(definition.ability))
		throw std::runtime_error("Ability grant definition unavailable");
	install_ability_grant(runtime, source, owner, parent, operation.effect, definition, ecs.get<Clock>().tick);
}

auto Runtime::ability_effect_invocation(EntityId ship, const AbilityGrantState &g,
                                        const EffectGroupDefinition &group) const -> EffectGroupInvocation
{
	NativeRuleContext context;
	context.owner = {ship};
	if (g.source == "officer")
		context.owner.officer = g.owner;
	else
		context.owner.equipment = g.owner;
	context.source = context.instigator = context.owner;
	context.subject = {ship};
	context.target = {g.target ? g.target : ship};
	context.hit = rule_hit_context(context.subject);
	context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	EffectGroupInvocation frame;
	frame.execution = group;
	frame.protected_ship = ship;
	frame.attacker = context.target.entity;
	frame.source = "ability." + std::to_string(ship) + "." + ability_recovery_key(g);
	frame.original_equipment = context.source.equipment;
	frame.rule_context = std::move(context);
	// Ability content predates native damage roles: its protected/attacker
	// aliases mean caster/selected recipient, independently of who instigated it.
	const auto role = [](std::string &value)
	{
		if (value == "protected")
			value = "subject";
		else if (value == "attacker")
			value = "target";
	};
	for (auto &operation : frame.execution.operations)
		role(operation.target);
	for (auto &cost : frame.execution.costs)
		role(cost.payer);
	return frame;
}

auto Runtime::ability_apply_groups(EntityId ship, const AbilityGrantState &g,
                                   const std::optional<EffectGroupDefinition> &start,
                                   const std::optional<EffectGroupDefinition> &activation) -> std::string
{
	std::vector<EffectGroupInvocation> frames;
	if (start)
		frames.push_back(ability_effect_invocation(ship, g, *start));
	if (activation)
		frames.push_back(ability_effect_invocation(ship, g, *activation));
	if (frames.empty())
		return {};
	auto quote = prepare_effect_groups(frames);
	return commit_effect_group(quote);
}

auto Runtime::ability_request_result(EntityId ship, const AbilityGrantState &g, bool accepted) -> void
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

auto Runtime::activate_ability(EntityId ship, const std::string &key, bool activation_committed) -> void
{
	const auto *before = ability_execution(ship, key);
	if (!before)
		return;
	const auto snapshot = *before;
	const auto &d = definition(ship).abilities.catalog->definitions.at(snapshot.definition);
	ConsoleContacts contacts;
	const auto *control = resolve_ability_control(ship, snapshot);
	const auto blocker = control ? ability_blocker(ship, *control, snapshot, snapshot.target, contacts)
	                             : "No compatible ability control";
	if (!control || !blocker.empty() || !ability_apparatus_blocker(ship, snapshot).empty())
	{
		auto &state = entities.at(ship).get_mut<AbilityRuntime>();
		auto &interrupted = state.grants.at(key);
		if (control && d.sustained && blocker == "Ability requirements unavailable" &&
		    entities.at(ship)
		            .get<EntityControls>()
		            .controllers.at(control->controller)
		            .definition.commands.at(control->enabled)
		            .semantics == CommandSemantics::Request)
			interrupted.retained_request = true;
		stop_ability(ship, state, interrupted);
		ability_request_result(ship, snapshot, false);
		return;
	}
	if (entities.at(ship).get<AbilityRuntime>().next_activation >= 9007199254740991ULL)
		throw std::runtime_error("Ability activation identity exhausted");
	const auto failure =
	    activation_committed ? std::string{} : ability_apply_groups(ship, snapshot, d.activation_effect);
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
	g.retained_request = false;
	g.activation = runtime.next_activation++;
	g.phase = d.sustained ? "active" : "ready";
	g.progress = d.preparation_ticks * 1000;
	if (d.recovery_on == "activation" || !d.sustained)
		ability_begin_recovery(ship, runtime, g);
}

auto Runtime::prepare_abilities(EntityId ship) -> void
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
			g.retained_request = false;
			g.blocker = "No compatible ability control";
			continue;
		}
		const auto &d = definition(ship).abilities.catalog->definitions.at(g.definition);
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
				g.retained_request = false;
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
		if (g.retained_request && g.control != c->id)
			g.retained_request = false;
		const bool requested =
		    discrete ? ability_running(g) || start_request || g.retained_request : command.value != 0;
		if (ability_running(g) && (g.control != c->id || g.target != target || !requested))
			stop_ability(ship, entities.at(ship).get_mut<AbilityRuntime>(), g);
		g.control = c->id;
		g.apparatus = c->apparatus.empty() && g.source == "equipment" ? g.owner : c->apparatus;
		g.requested = requested;
		if (!requested)
		{
			g.retained_request = false;
			continue;
		}
		g.blocker = ability_blocker(ship, *c, g, target, cache);
		if (!g.blocker.empty())
		{
			g.retained_request = discrete && d.sustained && !new_request &&
			                     (ability_running(g) || g.retained_request) &&
			                     g.blocker == "Ability requirements unavailable";
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
		if (ability_running(g) || (discrete && !start_request && !g.retained_request))
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
		// Automatic retained retries have no new request receipt. Explicit starts
		// retain their receipt through preparation so activation failure can reject it.
		g.request_id = discrete && !g.retained_request ? command.request_id : 0;
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
		if (entities.at(ship).get<AbilityRuntime>().next_activation >= 9007199254740991ULL)
			throw std::runtime_error("Ability activation identity exhausted");
		const auto failure = ability_apply_groups(ship, snapshot, d.start_effect,
		                                          d.preparation_ticks ? std::nullopt : d.activation_effect);
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
		started.retained_request = false;
		started.phase = "preparing";
		started.progress = 0;
		for (const auto &recovery : ability_recovery_reservations(ship, started))
			updated.recovery.try_emplace(recovery, 0);
		if (!d.preparation_ticks)
			activate_ability(ship, key, true);
	}
}

auto Runtime::request_ability_resources(EntityId ship) -> void
{
	auto entity = entities.at(ship);
	auto *power = entity.try_get_mut<ShipResources>();
	// Choir apparatus has no independent operating demand. Each ability joins
	// the shared work bundle with its own exact resource and quantity inputs.
	if (power)
		for (const auto &[id, choir] : definition(ship).psychic.choirs)
			if (const auto fit = instance(entity, id); fit && fit->entity.has<ResourceBinding>())
				request_system_resource(*power, fit->entity.get<ResourceBinding>().slot, 0, false, 0);
	for (const auto &[key, grant] : entity.get<AbilityRuntime>().grants)
	{
		if (!ability_running(grant) || !ability_apparatus_blocker(ship, grant).empty())
			continue;
		const auto &ability = definition(ship).abilities.catalog->definitions.at(grant.definition);
		auto costs = ability.costs;
		const auto preparing = grant.phase == "preparing" || grant.resume_phase == "preparing";
		const auto energy = preparing ? ability.preparation_energy_per_second : ability.energy_per_second;
		if (energy)
		{
			const auto fit = instance(entity, grant.apparatus);
			if (!power || !fit || !fit->entity.has<ResourceBinding>())
				continue;
			const auto slot = fit->entity.get<ResourceBinding>().slot;
			const auto reserve = power->definition.systems.at(slot).store;
			const auto &store = power->network.definition().stores.at(reserve);
			EffectCostDefinition cost;
			cost.payer = "subject";
			cost.equipment = grant.apparatus;
			cost.store = "energy:" + store.id;
			cost.resource = store.resource;
			cost.amount = energy;
			costs.push_back(std::move(cost));
		}
		stage_ability_upkeep(ship, grant, ability, costs);
	}
}

auto Runtime::ability_completed_work(const RuleActor &actor, const std::string &family) const
    -> std::optional<std::int64_t>
{
	const auto entity = rule_entity(actor);
	const auto *runtime = entity ? entity.try_get<AbilityRuntime>() : nullptr;
	if (!runtime || runtime->work_tick != ecs.get<Clock>().tick)
		return {};
	std::int64_t output = 0;
	for (const auto &work : runtime->work)
		if ((family.empty() || family == work.family) &&
		    (actor.officer.empty() || (work.source == "officer" && work.owner == actor.officer)) &&
		    (actor.equipment.empty() || (work.source == "equipment" && work.owner == actor.equipment)))
			output += work.output;
	return output;
}
} // namespace phoenix::world_detail
