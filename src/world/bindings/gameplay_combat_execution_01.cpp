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
auto Runtime::projectile_rows() const -> std::vector<flecs::entity>
{
	std::vector<flecs::entity> rows;
	projectile_query.each(
	    [&](flecs::entity e, const Projectile &)
	    {
		    rows.push_back(e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](auto a, auto b)
	          {
		          return a.template get<Projectile>().id < b.template get<Projectile>().id;
	          });
	return rows;
}

auto Runtime::combat_rows() const -> std::vector<std::pair<EntityId, flecs::entity>>
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	combat_ships.each(
	    [&](flecs::entity e, const StableId &id, const CombatStructure &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}

auto Runtime::duel_ended() const -> bool
{
	if (!combat_duel)
		return false;
	for (const auto &[id, e] : combat_rows())
		if (combat_dead(e))
			return true;
	return false;
}

auto Runtime::combat_dead(flecs::entity ship) const -> bool
{
	if (ship.has<MissionDeparture>())
		return true;
	const auto *d = ship.try_get<Destructible>();
	return d && d->destroyed_tick >= 0;
}

auto Runtime::structurally_disabled(flecs::entity ship, const std::string &equipment) const -> bool
{
	const auto *structure = ship.try_get<CombatStructure>();
	if (!structure)
		return false;
	const auto fit = instance(ship, equipment);
	return equipment_structurally_disabled(structure, equipment, fit && fit->kind == OperatingSystem::Team,
	                                       combat_dead(ship));
}

auto Runtime::combat_damage(flecs::entity ship, std::size_t compartment, std::int64_t amount, const Projectile *payload,
                            const NativeRuleContext *origin, const std::vector<NativeRuleSource> *sources,
                            std::int64_t *native_inflicted) -> std::int64_t
{
	const auto local_sources = sources ? std::vector<NativeRuleSource>{} : rule_sources(ship);
	if (!sources)
		sources = &local_sources;
	if (combat_dead(ship))
		return 0;
	auto context = damage_rule_context(
	    {ship.get<StableId>().id, PayloadOutcomeKind::Compartment, static_cast<std::int64_t>(compartment)}, amount, 0,
	    payload, origin);
	if (amount > 0)
		amount = apply_pre_damage_rules(ship, RuleTrigger::PreCompartmentDamage, context, sources);
	auto &s = ship.get_mut<CombatStructure>();
	const auto actual = native_compartment_absorption(s, compartment, amount);
	if (native_inflicted)
		*native_inflicted += actual;
	if (payload)
		queue_payload_outcome(*payload, context.hit, actual);
	if (amount > 0)
	{
		personnel_casualties(ship, compartment, amount);
		std::vector<std::string> affected;
		damage_faults(ship, compartment, amount, payload ? &affected : nullptr, {}, &context, sources);
		if (payload)
			for (const auto &equipment : affected)
				queue_payload_outcome(*payload,
				                      {ship.get<StableId>().id, PayloadOutcomeKind::System,
				                       static_cast<std::int64_t>(compartment), equipment},
				                      actual);
		if (apply_native_compartment_damage(s, compartment, amount))
		{
			ship.get_mut<Destructible>().destroyed_tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
			emit_native_destroy_rules(ship, {ship.get<StableId>().id}, sources);
			ship.get_mut<Motion>().velocity = {};
		}
	}
	if (context.presented > 0)
	{
		context.inflicted = actual;
		context.residual = amount - actual;
		emit_damage_rules(ship, RuleTrigger::PostCompartmentDamage, context, nullptr, sources);
	}
	// Translate actual hull absorption back into the incoming projectile budget.
	// Damage rules still see the complete presented hit exactly once.
	return incoming_damage_absorbed(actual, context.presented, amount);
}

auto Runtime::operate_combat(flecs::entity ship, flecs::entity child, OperatingRequest output, bool execute) -> bool
{
	auto &m = child.get_mut<CombatMechanism>();
	if (m.definition.kind == OperatingSystem::Team && away_reserved(ship.get<StableId>().id, "team:" + m.definition.id))
		return false;
	if (m.definition.kind == OperatingSystem::Team && !m.get<TeamState>().order.empty() &&
	    output.action != OperatingAction::SystemEnabled)
		return false;
	if (!output.arguments.empty())
		return false;
	if (output.action == OperatingAction::SystemEnabled)
	{
		if (output.value != 0 && output.value != 1)
			return false;
		if (execute)
			child.get_mut<Equipment>().enabled = output.value != 0;
		return true;
	}
	if (output.action == OperatingAction::MagazineSelect && m.definition.kind == OperatingSystem::Weapon)
	{
		if (output.value < 0 ||
		    output.value >= static_cast<std::int64_t>(m.definition.get<WeaponDefinition>().magazines.size()))
			return false;
		if (execute && m.get<WeaponState>().selected_magazine != output.value)
		{
			if (m.get<WeaponState>().loaded_magazine >= 0 && !m.get<WeaponState>().unloading)
				m.get<WeaponState>().unloading = load_duration(ship, m, m.get<WeaponState>().loaded_magazine);
			m.get<WeaponState>().selected_magazine = output.value;
			m.get<WeaponState>().loading = 0;
			if (child.has<DirectedRuntime>())
				child.get_mut<DirectedRuntime>().loading_mode.clear();
		}
		return true;
	}
	if (output.action == OperatingAction::WeaponMode && m.definition.kind == OperatingSystem::Weapon &&
	    m.definition.get<WeaponDefinition>().point_defence)
	{
		if (output.value < 0 || output.value > 2)
			return false;
		const auto kind = output.value == 0   ? DirectedTargetingKind::Off
		                  : output.value == 1 ? DirectedTargetingKind::Intercept
		                                      : DirectedTargetingKind::Designated;
		const auto *directed = directed_definition(ship.get<StableId>().id, m.definition.id);
		if (directed)
			for (std::size_t i = 0; i < directed->targeting.size(); ++i)
				if (directed->targeting[i].kind == kind)
					return directed_setting(ship.get<StableId>().id, m.definition.id, "targeting_mode", i, execute);
		return false;
	}
	if (output.action == OperatingAction::ShieldDischarge && m.definition.kind == OperatingSystem::Shield)
	{
		if (output.value < 0 || output.value > 1)
			return false;
		if (execute)
			for (auto &c : ship.get_mut<ShieldComponent>().contributions)
				if (c.equipment == m.definition.id)
					c.state.discharge = output.value != 0;
		return true;
	}
	if (output.action == OperatingAction::RepairSystem && m.definition.kind == OperatingSystem::Team &&
	    m.definition.get<TeamDefinition>().capabilities.contains("repair"))
	{
		const auto &s = ship.get<CombatStructure>();
		const auto hull = static_cast<std::int64_t>(s.definition.equipment_compartments.size()) + 1;
		if (output.value < 0 || output.value > hull || (output.value == hull && m.get<TeamState>().destination < 0))
			return false;
		if (execute)
		{
			m.get<TeamState>().repair_system = output.value == hull ? -2 : output.value - 1;
			m.get<TeamState>().repair_auto = output.value == 0 && !m.get<TeamState>().compartment_assignment;
			if (!output.value && !m.get<TeamState>().compartment_assignment)
				m.get<TeamState>().destination = -1;
			if (output.value && output.value != hull)
			{
				const bool assigned =
				    manual_team_destination(enabled_system_owner(ship.get<StableId>().id, m.definition.id));
				auto it = s.definition.equipment_compartments.begin();
				std::advance(it, m.get<TeamState>().repair_system);
				const auto target = s.definition.compartment_ids.at(it->second);
				if (assigned && m.get<TeamState>().compartment_assignment && m.get<TeamState>().destination != target)
					m.get<TeamState>().repair_system = -1;
				else
				{
					m.get<TeamState>().compartment_assignment =
					    assigned || m.definition.get<TeamDefinition>().local_repair_priority;
					m.get<TeamState>().destination = target;
				}
			}
		}
		return true;
	}
	if (output.action == OperatingAction::WeaponTarget && m.definition.kind == OperatingSystem::Weapon)
	{
		return directed_setting(ship.get<StableId>().id, m.definition.id, "target", output.value, execute);
	}
	if (output.action == OperatingAction::RepairCompartment && m.definition.kind == OperatingSystem::Team &&
	    m.definition.get<TeamDefinition>().capabilities.contains("repair"))
	{
		const auto count = static_cast<std::int64_t>(ship.get<CombatStructure>().compartments.size());
		if (output.value < 0 || output.value > count)
			return false;
		if (execute)
		{
			if (output.value == 0 &&
			    manual_team_destination(enabled_system_owner(ship.get<StableId>().id, m.definition.id)))
			{
				auto &team = m.get<TeamState>();
				if (!team.compartment_assignment)
					team.destination = team.travel ? team.edge_to : team.location;
				team.compartment_assignment = true;
				team.repair_auto = false;
				return true;
			}
			m.get<TeamState>().destination = output.value - 1;
			m.get<TeamState>().repair_auto = output.value == 0;
			m.get<TeamState>().repair_system = -1;
			m.get<TeamState>().compartment_assignment =
			    output.value != 0 && m.definition.get<TeamDefinition>().local_repair_priority;
		}
		return true;
	}
	return false;
}

auto Runtime::combat_telemetry(flecs::entity ship, flecs::entity child, std::map<std::string, std::int64_t> &v) const
    -> void
{
	const auto &fit = child.get<Equipment>();
	const bool stopped = structurally_disabled(ship, fit.id);
	const auto *structure = ship.try_get<CombatStructure>();
	const auto compartment = structure ? structure->definition.equipment_compartments.find(fit.id)
	                                   : std::map<std::string, std::string>::const_iterator{};
	v["structural_disabled"] =
	    structure && compartment != structure->definition.equipment_compartments.end() &&
	    structure->compartments.at(structure->definition.compartment_ids.at(compartment->second)).disabled;
	v["officer_condition"] = 0;
	const auto id = ship.get<StableId>().id;
	const auto &pd = definition(id).personnel;
	const auto &ps = ship.get<PersonnelState>();
	for (std::size_t i = 0; i < ps.officers.size(); ++i)
		if (const auto *duty = personnel_duty(id, ps.officers[i].duty); duty && duty_affects_system(id, *duty, fit.id))
		{
			const auto &condition = ps.officers[i].condition;
			const auto severity = condition == "stunned" ? 1 : condition == "minor" ? 2 : condition == "severe" ? 3 : 0;
			v["officer_condition"] = std::max(v["officer_condition"], static_cast<std::int64_t>(severity));
		}
	v["damaged"] = fault_effectiveness(ship, fit.id) < 1000;
	v["destroyed"] = 0;
	v["overheated"] = 0;
	if (structure && structure->faults.contains(fit.id))
		for (std::size_t i = 0; i < structure->faults.at(fit.id).size(); ++i)
			if (structure->faults.at(fit.id)[i].active && structure->definition.faults.at(fit.id)[i].destroys)
				v["destroyed"] = 1;
	if (const auto *power = ship.try_get<ShipResources>())
	{
		const auto slot = child.get<ResourceBinding>().slot;
		const auto store = power->definition.systems.at(slot).store;
		if (store != no_resource)
			v["overheated"] = power->network.stores()[store].thermal_stop;
	}

	if (stopped)
		v["enabled"] = 0;
	if (const auto *m = child.try_get<CombatMechanism>())
	{
		// The public telemetry vocabulary remains stable; absent capabilities project neutral values.
		for (const auto *key : {"target", "requested", "ready", "heading_mdeg", "ammo", "capacity", "reload_progress",
		                        "shots", "team_location", "travel_remaining"})
			v[key] = 0;
		v["reload_ticks"] = 1;
		v["team_destination"] = -1;
		switch (m->definition.kind)
		{
		case OperatingSystem::Weapon:
		{
			const auto &w = m->get<WeaponState>();
			v["target"] = v["requested"] = directed_target(ship.get<StableId>().id, m->definition.id);
			v["ready"] = w.ready;
			v["shots"] = w.shots;
			break;
		}
		case OperatingSystem::Turret:
			v["target"] = v["requested"] = directed_mount_target(ship, child);
			v["heading_mdeg"] = m->get<TurretState>().heading;
			break;
		case OperatingSystem::Magazine:
		{
			const auto &stock = physical_stock(ship, m->get<MagazineState>().stock);
			v["ammo"] = stock.quantity(stock.selected());
			v["capacity"] = stock.selected().empty() ? 0 : stock.definition().capacity / stock.size(stock.selected());
			v["reload_progress"] = 0;
			v["reload_rate_permille"] = effective_property(ship, fit.id, PropertyKey::reload, 1000);
			v["reload_ticks"] = 0;
			if (const auto &definition = m->definition.get<MagazineDefinition>().processor)
			{
				const auto &processor = ship.get<ShipProcessors>().values.at(definition->id);
				v["reload_progress"] = processor.state().progress / 1000;
				for (const auto &recipe : definition->recipes)
					if (recipe.id == processor.state().recipe)
						v["reload_ticks"] = recipe.work;
			}
			break;
		}
		case OperatingSystem::Team:
			v["team_revision"] = m->get<TeamState>().revision;
			v["requested"] = m->get<TeamState>().destination + 1;
			v["team_location"] = m->get<TeamState>().location;
			v["team_destination"] = m->get<TeamState>().destination;
			v["travel_remaining"] = m->get<TeamState>().travel;
			break;
		case OperatingSystem::Shield:
			v["capacity"] = m->definition.reserve().capacity;
			break;
		default:
			break;
		}
	}
}

auto Runtime::apply_structural_disablement() -> void
{
	// Stop actuation without overwriting accepted enable/mode settings.
	for (const auto &[id, ship] : combat_rows())
		if (ship.has<CombatStructure>())
		{
			if (combat_dead(ship))
			{
				ship.remove<Movable>();
				ship.get_mut<Motion>().velocity = {};
			}
		}
}

auto Runtime::prepare_combat(flecs::entity ship) -> void
{
	if (!ship.has<CombatStructure>())
		return;
	if (!duel_ended())
		prepare_weapons(ship);
	else
		for (auto child : ship.get<EquipmentIndex>().values)
			if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
				m->get<WeaponState>().ready = false;
}

auto Runtime::request_combat_resources(flecs::entity ship) -> void
{
	request_shields(ship);
	auto *power = ship.try_get_mut<ShipResources>();
	if (!power)
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			const auto index = child.get<ResourceBinding>().slot;
			if (d.kind == OperatingSystem::Magazine)
				request_system_resource(*power, index, 0, false, 0);
		}
}

auto Runtime::actuate_combat(flecs::entity ship) -> void
{
	if (!ship.has<CombatStructure>() || duel_ended())
	{
		release_directed_operations(ship);
		return;
	}
	actuate_shields(ship);
	fire_weapons(ship);
}

auto Runtime::penetrate(Projectile &p, flecs::entity ship, fixed::Vec start, fixed::Vec end) -> void
{
	const auto sources = rule_sources(ship);
	auto &s = ship.get_mut<CombatStructure>();
	if (combat_dead(ship))
		return;
	const auto id = ship.get<StableId>().id;
	const bool first_contact = p.shield_hits.insert(id).second;
	std::int64_t native_inflicted = 0;
	const auto presented = p.damage;
	const auto finish_entity_hit = [&]()
	{
		const auto actual = native_inflicted;
		if (first_contact)
			queue_payload_outcome(p, {id}, actual);
		if (first_contact || actual)
		{
			auto context = damage_rule_context({id}, presented, p.source, &p);
			context.inflicted = actual;
			context.residual = std::max<std::int64_t>(0, presented - actual);
			emit_damage_rules(ship, RuleTrigger::PostEntityDamage, context, nullptr, &sources);
		}
	};
	if (first_contact)
		p.damage = absorb_shields(ship, p.heading, p.damage, p.definition.shield_bypass,
		                          p.continuous ? &p.bypass_remainder : nullptr, p.source, &p, nullptr, &sources,
		                          &native_inflicted);
	if (!p.damage)
	{
		finish_entity_hit();
		// Effect-only contact delivers entity bindings without inventing penetration.
		if (!p.definition.damage)
			p.life = 0;
		return;
	}
	PenetrationTraversal traversal(p, s, id, ship.get<Pose>(), start, end);
	while (const auto contact = traversal.next(p, s))
	{
		const auto absorbed =
		    combat_damage(ship, contact->compartment, contact->deposit, &p, nullptr, &sources, &native_inflicted);
		traversal.resolve(p, absorbed, ship.get<CombatStructure>().compartments.at(contact->compartment).hp == 0);
	}
	finish_entity_hit();
}

auto Runtime::repair_route(flecs::entity ship, std::size_t from, std::size_t to, std::int64_t *total_cost) const
    -> std::pair<std::int64_t, std::int64_t>
{
	const auto &s = ship.get<CombatStructure>();
	return physical_route(ship.get<StableId>().id, s.compartments.size(), s.definition.connections, from, to,
	                      total_cost);
}

auto Runtime::repair_candidate(flecs::entity ship, std::size_t compartment, const std::string &equipment,
                               const std::set<RepairFault> &claimed) const -> RepairCandidate
{
	return native_repair_candidate(ship.get<CombatStructure>(), compartment, equipment, claimed);
}

auto Runtime::advance_repair_team(flecs::entity ship, flecs::entity child) -> bool
{
	auto &s = ship.get_mut<CombatStructure>();
	auto &m = child.get_mut<CombatMechanism>();
	if (combat_dead(ship) || !child.get<Equipment>().enabled || structurally_disabled(ship, m.definition.id) ||
	    !team_available(m))
		return false;
	const auto &r = m.get<TeamState>();
	return advance_native_repair_team(
	    s, m, r.travel <= 0 || physical_edge_open(ship.get<StableId>().id, r.location, r.edge_to));
}

auto Runtime::route_repair_team(flecs::entity ship, TeamState &r, std::size_t target) -> void
{
	const auto route = repair_route(ship, r.location, target);
	r.edge_to = route.first;
	r.travel = r.edge_total = route.second;
}

auto Runtime::structural_repair_work(flecs::entity ship, std::vector<flecs::entity> teams, EntityId target,
                                     std::int64_t compartment, const std::string &work_key, std::int64_t progress,
                                     std::int64_t required) -> std::int64_t
{
	std::erase_if(teams,
	              [&](auto team)
	              {
		              return !team.template get<Equipment>().enabled ||
		                     team_work(ship, team.template get<CombatMechanism>(), PropertyKey::repair, 1000) <= 0;
	              });
	std::sort(teams.begin(), teams.end(),
	          [](auto a, auto b)
	          {
		          const auto &x = a.template get<CombatMechanism>().definition;
		          const auto &y = b.template get<CombatMechanism>().definition;
		          return x.template get<TeamDefinition>().repair_rate != y.template get<TeamDefinition>().repair_rate
		                     ? x.template get<TeamDefinition>().repair_rate >
		                           y.template get<TeamDefinition>().repair_rate
		                     : x.id < y.id;
	          });
	const auto origin = ship.get<StableId>().id;
	std::int64_t work = 0;
	for (std::size_t rank = 0; rank < teams.size(); ++rank)
	{
		const auto &mechanism = teams[rank].get<CombatMechanism>();
		const auto &d = mechanism.definition;
		if (!d.get<TeamDefinition>().capabilities.contains("repair"))
			continue;
		const auto contribution = native_repair_contribution(
		    d.get<TeamDefinition>(),
		    team_work(ship, mechanism, PropertyKey::repair, d.get<TeamDefinition>().repair_rate * 1000000), rank);
		std::int64_t credit = 0;
		if (const auto *inventory = ship.try_get<MaterialInventory>();
		    inventory && inventory->work_credit.contains(d.id))
			credit = inventory->work_credit.at(d.id);
		const auto *consumer = material_consumer(origin, d.id);
		const bool native_priced =
		    consumer && consumer->costs.contains("structural") && !consumer->costs.at("structural").empty();
		auto unit_costs = native_material_work_costs(origin, d.id, "structural", false);
		std::vector<std::int64_t> available;
		if (unit_costs)
			for (const auto &cost : *unit_costs)
				available.push_back(cost.amount > 0 ? named_stock(ship, cost.store).stock.available(cost.resource) : 0);
		const auto plan = plan_native_repair_materials(contribution, credit, unit_costs, available);
		const auto desired = plan.work;
		if (desired <= 0)
			continue;
		auto purchases = plan.purchases;
		auto costs = plan.costs;
		if (!unit_costs)
			unit_costs = std::vector<EffectCostDefinition>{};
		RuleActor actor{target};
		std::string key = std::to_string(target) + "/work/" + work_key;
		std::int64_t before_progress = progress + work, threshold = required;
		if (compartment >= 0)
		{
			const auto &structure = entities.at(target).get<CombatStructure>();
			actor.kind = PayloadOutcomeKind::Compartment;
			actor.compartment = compartment;
			key = std::to_string(target) + "/compartment/" + structure.definition.compartments.at(compartment).id;
			if (!work_key.empty())
				key += "/" + work_key;
			before_progress =
			    structure.compartments.at(compartment).hp * rate * 1000 + structure.compartments.at(compartment).work;
			threshold = structure.definition.compartments.at(compartment).maximum * rate * 1000;
		}
		auto quote =
		    prepare_finite_native_work(origin, "repair", d.id, key, actor, before_progress, threshold, desired, costs);
		if (!quote)
			continue;
		const auto repriced = reprice_native_repair_materials(quote->work, credit, *unit_costs);
		const auto actual_purchases = repriced.purchases;
		if (actual_purchases != purchases)
		{
			purchases = actual_purchases;
			costs = repriced.costs;
			quote = prepare_finite_native_work(origin, "repair", d.id, key, actor, before_progress, threshold,
			                                   quote->work, costs, false);
			if (!quote)
				continue;
		}
		std::int64_t after_progress = before_progress + quote->work;
		bool completed = threshold > 0 && after_progress >= threshold;
		if (compartment >= 0)
		{
			auto &prepared = prepare_structure_outcome(quote->prepared, target);
			const auto &definition_compartment =
			    entities.at(target).get<CombatStructure>().definition.compartments.at(compartment);
			native_structural_repair(prepared.compartments.at(compartment), definition_compartment, quote->work,
			                         rate * 1000);
			after_progress =
			    prepared.compartments.at(compartment).hp * rate * 1000 + prepared.compartments.at(compartment).work;
			completed = prepared.compartments.at(compartment).hp == definition_compartment.maximum;
		}
		if (!commit_finite_native_payment(*quote))
			continue;
		if (native_priced)
			ship.get_mut<MaterialInventory>().work_credit[d.id] =
			    remaining_native_repair_credit(credit, purchases, quote->work);
		work += quote->work;
		commit_finite_native_work(*quote, quote->work, std::min(threshold, after_progress), threshold, completed);
	}
	return work;
}

auto Runtime::apply_structural_repair(CombatStructure &structure, std::size_t index, std::int64_t work) -> void
{
	(void)native_structural_repair(structure, index, work, rate * 1000);
}

auto Runtime::finish_fault_repair(CombatStructure &structure, const std::string &system, std::size_t index) -> void
{
	(void)native_finish_fault_repair(structure, system, index);
}

auto Runtime::step_repairs() -> void
{
	if (duel_ended())
		return;
	for (const auto &[id, ship] : combat_rows())
	{
		auto &s = ship.get_mut<CombatStructure>();
		if (combat_dead(ship))
			continue;
		std::map<std::size_t, std::vector<flecs::entity>> structural_workers;
		std::set<std::pair<std::string, std::size_t>> claimed;
		for (auto child : ship.get<EquipmentIndex>().values)
			if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
			{
				if (!m->definition.get<TeamDefinition>().capabilities.contains("repair") ||
				    (!m->get<TeamState>().order.empty() && m->get<TeamState>().activity != "repair") ||
				    away_reserved(id, "team:" + m->definition.id))
					continue;
				if (m->get<TeamState>().order.empty()
				        ? !advance_repair_team(ship, child)
				        : m->get<TeamState>().travel > 0 || m->get<TeamState>().destination < 0 ||
				              m->get<TeamState>().location != m->get<TeamState>().destination)
					continue;
				if (m->get<TeamState>().repair_auto)
				{
					m->get<TeamState>().destination = -1;
					std::vector<RepairRoute> routes;
					for (std::size_t i = 0; i < s.compartments.size(); ++i)
					{
						std::int64_t cost = 0;
						const auto route = repair_route(ship, m->get<TeamState>().location, i, &cost);
						routes.push_back({i, route.first, cost});
					}
					select_automatic_repair_destination(s, m->get<TeamState>(), routes, claimed);
				}
				if (m->get<TeamState>().destination < 0)
					continue;
				if (m->get<TeamState>().destination != m->get<TeamState>().location)
				{
					route_repair_team(ship, m->get<TeamState>(), m->get<TeamState>().destination);
					continue;
				}
				const auto index = static_cast<std::size_t>(m->get<TeamState>().location);
				auto candidate = select_native_repair_task(s, m->get<TeamState>(), claimed);
				const auto &task = candidate.fault;
				if (task)
				{
					claimed.insert(*task);
					auto &f = s.faults.at(task->first)[task->second];
					const auto &d = s.definition.faults.at(task->first)[task->second];
					const auto desired = team_work(ship, *m, PropertyKey::repair,
					                               m->definition.get<TeamDefinition>().repair_rate * 1000);
					const auto costs = native_material_work_costs(id, m->definition.id, "repair", f.work > 0);
					if (desired <= 0 || !costs)
						continue;
					const auto required = d.repair_work * rate * 1000;
					const auto key = std::to_string(id) + "/fault/" + task->first + "/" + d.id;
					RuleActor actor{id, task->first};
					actor.kind = PayloadOutcomeKind::System;
					actor.compartment = index;
					auto quote = prepare_finite_native_work(id, "repair", m->definition.id, key, actor, f.work,
					                                        required, std::min(desired, required - f.work), *costs);
					if (!quote)
						continue;
					auto &prepared =
					    prepare_structure_outcome(quote->prepared, id).faults.at(task->first).at(task->second);
					if (!prepared.active || d.destroys)
						continue;
					prepared.work += quote->work;
					const auto progress = prepared.work;
					const bool completed = progress >= required;
					if (completed)
						native_finish_fault_repair(prepared, d);
					if (commit_finite_native_payment(*quote))
						commit_finite_native_work(*quote, quote->work, std::min(required, progress), required,
						                          completed);
				}
				else if (candidate.score > 0)
					structural_workers[index].push_back(child);
			}
		for (auto &[index, teams] : structural_workers)
		{
			(void)structural_repair_work(ship, teams, id, static_cast<std::int64_t>(index));
		}
	}
}

auto Runtime::collision_damage(EntityId a, EntityId b, const CollisionBody &before_a, const CollisionBody &before_b)
    -> void
{
	if (duel_ended())
		return;
	for (const auto id : {a, b})
		if (auto ship = entities.at(id); ship.has<CombatStructure>())
			if (const auto impact = native_collision_impact(ship.get<CombatStructure>().definition, ship.get<Pose>(),
			                                                before_a, before_b, id == a))
			{
				NativeRuleContext origin;
				origin.source = origin.instigator = RuleActor{id == a ? b : a};
				combat_damage(ship, impact->compartment, impact->damage, nullptr, &origin);
			}
}

auto Runtime::repair_team_present(flecs::entity ship, std::size_t compartment) const -> bool
{
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *team = child.try_get<CombatMechanism>();
		    team && team->definition.kind == OperatingSystem::Team &&
		    team->definition.get<TeamDefinition>().capabilities.contains("repair"))
		{
			const auto &state = team->get<TeamState>();
			if (state.location == static_cast<std::int64_t>(compartment) && state.travel == 0 &&
			    team_available(*team) && child.get<Equipment>().enabled &&
			    !structurally_disabled(ship, team->definition.id) &&
			    !away_reserved(ship.get<StableId>().id, "team:" + team->definition.id))
				return true;
		}
	return false;
}
} // namespace phoenix::world_detail
