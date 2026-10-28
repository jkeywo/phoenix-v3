#include "combat_effects.hpp"
// World::Impl combat adapter. All decisions originate at Controllers; mechanisms consume admitted intent.
std::vector<flecs::entity> projectile_rows() const
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
std::vector<std::pair<EntityId, flecs::entity>> combat_rows() const
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
std::uint64_t next_projectile = 1;
bool combat_duel = false;
bool duel_ended() const
{
	if (!combat_duel)
		return false;
	for (const auto &[id, e] : combat_rows())
		if (combat_dead(e))
			return true;
	return false;
}
bool combat_dead(flecs::entity ship) const
{
	if (ship.has<MissionDeparture>())
		return true;
	const auto *d = ship.try_get<Destructible>();
	return d && d->destroyed_tick >= 0;
}
bool structurally_disabled(flecs::entity ship, const std::string &equipment) const
{
	const auto *structure = ship.try_get<CombatStructure>();
	if (!structure)
		return false;
	const auto fit = instance(ship, equipment);
	return equipment_structurally_disabled(structure, equipment, fit && fit->kind == OperatingSystem::Team,
	                                       combat_dead(ship));
}
std::int64_t combat_damage(flecs::entity ship, std::size_t compartment, std::int64_t amount,
                           const Projectile *payload = nullptr, const NativeRuleContext *origin = nullptr,
                           const std::vector<NativeRuleSource> *sources = nullptr,
                           std::int64_t *native_inflicted = nullptr)
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
	const auto actual = std::min(amount, s.compartments.at(compartment).hp);
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
		auto &c = s.compartments.at(compartment);
		c.hp = std::max<std::int64_t>(0, c.hp - amount);
		if (!c.hp)
			c.disabled = true;
		if (std::all_of(s.compartments.begin(), s.compartments.end(),
		                [](const auto &v)
		                {
			                return v.hp == 0;
		                }))
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
	return amount > 0 ? std::min(context.presented, (actual * context.presented + amount - 1) / amount) : 0;
}

bool operate_combat(flecs::entity ship, flecs::entity child, OperatingRequest output, bool execute)
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
void combat_telemetry(flecs::entity ship, flecs::entity child, std::map<std::string, std::int64_t> &v) const
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
void apply_structural_disablement()
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
#include "combat_weapons.hpp"
void prepare_combat(flecs::entity ship)
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
void request_combat_resources(flecs::entity ship)
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
void actuate_combat(flecs::entity ship)
{
	if (!ship.has<CombatStructure>() || duel_ended())
	{
		release_directed_operations(ship);
		return;
	}
	actuate_shields(ship);
	fire_weapons(ship);
}

void penetrate(Projectile &p, flecs::entity ship, fixed::Vec start, fixed::Vec end)
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
#include "combat_ordnance.hpp"
std::pair<std::int64_t, std::int64_t> repair_route(flecs::entity ship, std::size_t from, std::size_t to,
                                                   std::int64_t *total_cost = nullptr) const
{
	const auto &s = ship.get<CombatStructure>();
	return physical_route(ship.get<StableId>().id, s.compartments.size(), s.definition.connections, from, to,
	                      total_cost);
}

using RepairFault = std::pair<std::string, std::size_t>;
struct RepairCandidate
{
	std::int64_t score = 0;
	std::optional<RepairFault> fault;
};
RepairCandidate repair_candidate(flecs::entity ship, std::size_t compartment, const std::string &equipment = {},
                                 const std::set<RepairFault> &claimed = {}) const
{
	const auto &s = ship.get<CombatStructure>();
	const auto &c = s.compartments[compartment];
	const auto &d = s.definition.compartments[compartment];
	RepairCandidate best;
	if (equipment.empty() && c.hp < d.maximum)
		best.score = c.disabled ? 10000 : (d.maximum - c.hp) * 1000 / d.maximum;
	for (const auto &[id, defs] : s.definition.faults)
	{
		if (equipment_destroyed(ship, id) || (!equipment.empty() && equipment != id) ||
		    s.definition.compartment_ids.at(s.definition.equipment_compartments.at(id)) != compartment)
			continue;
		for (std::size_t i = 0; i < defs.size(); ++i)
		{
			const RepairFault key{id, i};
			if (!s.faults.at(id)[i].active || defs[i].destroys || claimed.contains(key))
				continue;
			const auto score =
			    (defs[i].effect == "disabled" || defs[i].effect == "feed" || !defs[i].effectiveness ? 10000 : 1000) +
			    defs[i].severity;
			if (score > best.score)
				best = {score, key};
		}
	}
	return best;
}
// Shared by ordinary repair orders and away staging. Inspect at each arrival.
bool advance_repair_team(flecs::entity ship, flecs::entity child)
{
	auto &s = ship.get_mut<CombatStructure>();
	auto &m = child.get_mut<CombatMechanism>();
	if (combat_dead(ship) || !child.get<Equipment>().enabled || structurally_disabled(ship, m.definition.id) ||
	    !team_available(m))
		return false;
	auto &r = m.get<TeamState>();
	if (r.travel > 0)
	{
		if (!physical_edge_open(ship.get<StableId>().id, r.location, r.edge_to))
			return false;
		if (--r.travel > 0)
			return false;
		r.location = r.edge_to;
		r.edge_to = -1;
		r.edge_total = 0;
	}
	if (m.definition.get<TeamDefinition>().capabilities.contains("repair"))
		for (auto &[equipment, states] : s.faults)
			if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) ==
			    static_cast<std::size_t>(r.location))
				for (auto &state : states)
					if (state.active)
						state.diagnosed = true;
	return true;
}
void route_repair_team(flecs::entity ship, TeamState &r, std::size_t target)
{
	const auto route = repair_route(ship, r.location, target);
	r.edge_to = route.first;
	r.travel = r.edge_total = route.second;
}
// Repair effort is milli-HP per second; compartment work carries sub-tick fractions.
std::int64_t structural_repair_work(flecs::entity ship, std::vector<flecs::entity> teams, EntityId target,
                                    std::int64_t compartment = -1, const std::string &work_key = {},
                                    std::int64_t progress = 0, std::int64_t required = 0)
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
		const auto contribution =
		    team_work(ship, mechanism, PropertyKey::repair, d.get<TeamDefinition>().repair_rate * 1000000) /
		    (1000 + d.get<TeamDefinition>().stack_penalty * static_cast<std::int64_t>(rank));
		std::int64_t credit = 0;
		if (const auto *inventory = ship.try_get<MaterialInventory>();
		    inventory && inventory->work_credit.contains(d.id))
			credit = inventory->work_credit.at(d.id);
		const auto *consumer = material_consumer(origin, d.id);
		const bool native_priced =
		    consumer && consumer->costs.contains("structural") && !consumer->costs.at("structural").empty();
		auto unit_costs = native_material_work_costs(origin, d.id, "structural", false);
		const bool inputs_available = unit_costs.has_value();
		if (!inputs_available && credit <= 0)
			continue;
		if (!unit_costs)
			unit_costs = std::vector<EffectCostDefinition>{};
		const auto work_unit = rate * 1000;
		std::int64_t desired = inputs_available ? contribution : std::min(contribution, credit);
		std::int64_t purchases =
		    unit_costs->empty() ? 0 : std::max<std::int64_t>(0, desired - credit + work_unit - 1) / work_unit;
		for (const auto &cost : *unit_costs)
			if (cost.amount > 0)
				purchases =
				    std::min(purchases, named_stock(ship, cost.store).stock.available(cost.resource) / cost.amount);
		if (!unit_costs->empty())
			desired = std::min(desired, credit + purchases * work_unit);
		if (desired <= 0)
			continue;
		auto costs = *unit_costs;
		for (auto &cost : costs)
			cost.amount *= purchases;
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
		const auto actual_purchases =
		    unit_costs->empty() ? 0 : std::max<std::int64_t>(0, quote->work - credit + work_unit - 1) / work_unit;
		if (actual_purchases != purchases)
		{
			purchases = actual_purchases;
			costs = *unit_costs;
			for (auto &cost : costs)
				cost.amount *= purchases;
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
			ship.get_mut<MaterialInventory>().work_credit[d.id] = credit + purchases * work_unit - quote->work;
		work += quote->work;
		commit_finite_native_work(*quote, quote->work, std::min(threshold, after_progress), threshold, completed);
	}
	return work;
}
void apply_structural_repair(CombatStructure &structure, std::size_t index, std::int64_t work)
{
	(void)native_structural_repair(structure, index, work, rate * 1000);
}
void finish_fault_repair(CombatStructure &structure, const std::string &system, std::size_t index)
{
	(void)native_finish_fault_repair(structure, system, index);
}

void step_repairs()
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
					std::tuple<std::int64_t, std::int64_t, std::int64_t> best{
					    0, std::numeric_limits<std::int64_t>::min(), 0};
					m->get<TeamState>().destination = -1;
					for (std::size_t i = 0; i < s.compartments.size(); ++i)
					{
						std::int64_t travel_cost = 0;
						const auto route = repair_route(ship, m->get<TeamState>().location, i, &travel_cost);
						if (i != static_cast<std::size_t>(m->get<TeamState>().location) && route.first < 0)
							continue;
						const auto score = repair_candidate(ship, i, {}, claimed).score;
						if (!score)
							continue;
						const auto key = std::tuple{score, -travel_cost, -static_cast<std::int64_t>(i)};
						if (key > best)
						{
							best = key;
							m->get<TeamState>().destination = i;
						}
					}
				}
				if (m->get<TeamState>().destination < 0)
					continue;
				if (m->get<TeamState>().destination != m->get<TeamState>().location)
				{
					route_repair_team(ship, m->get<TeamState>(), m->get<TeamState>().destination);
					continue;
				}
				std::string priority;
				if (m->get<TeamState>().repair_system >= 0)
				{
					auto at = s.definition.equipment_compartments.begin();
					std::advance(at, m->get<TeamState>().repair_system);
					priority = at->first;
				}
				const auto index = static_cast<std::size_t>(m->get<TeamState>().location);
				auto candidate = repair_candidate(ship, index, priority, claimed);
				if (!priority.empty() && !candidate.fault)
					candidate = repair_candidate(ship, index, {}, claimed);
				if (m->get<TeamState>().repair_system == -2 &&
				    s.compartments[index].hp < s.definition.compartments[index].maximum)
					candidate = {10000, {}};
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
void collision_damage(EntityId a, EntityId b, const CollisionBody &before_a, const CollisionBody &before_b)
{
	if (duel_ended())
		return;
	const auto direction = fixed::normal(before_b.position - before_a.position);
	const auto closing =
	    std::max<std::int64_t>(0, fixed::dot(before_a.velocity - before_b.velocity, direction) / fixed::unit);
	for (const auto id : {a, b})
		if (auto ship = entities.at(id); ship.has<CombatStructure>())
		{
			auto &s = ship.get_mut<CombatStructure>();
			if (closing < s.definition.collision_threshold)
				continue;
			const auto mass = before_a.mass && before_b.mass ? std::min(before_a.mass, before_b.mass)
			                                                 : std::max(before_a.mass, before_b.mass);
			const auto metres = std::min<std::int64_t>(closing / 1000, 1000);
			const auto damage =
			    std::min<std::int64_t>(1000000, std::clamp<std::int64_t>(mass / 1000000, 1, 1000000) * metres * metres /
			                                        100 * s.definition.collision_scale);
			const auto contact = combat_local(id == a ? before_b.position : before_a.position, ship.get<Pose>());
			std::size_t nearest = 0;
			std::int64_t best = std::numeric_limits<std::int64_t>::max();
			for (std::size_t i = 0; i < s.definition.compartments.size(); ++i)
			{
				const auto &c = s.definition.compartments[i];
				const auto distance = fixed::length(contact - fixed::Vec{c.x, c.y});
				if (distance < best)
				{
					best = distance;
					nearest = i;
				}
			}
			NativeRuleContext origin;
			origin.source = origin.instigator = RuleActor{id == a ? b : a};
			combat_damage(ship, nearest, damage, nullptr, &origin);
		}
}
bool repair_team_present(flecs::entity ship, std::size_t compartment) const
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
void project_combat(const ConsoleAddress &address, Observation &result) const
{
	const auto ship = entities.at(address.ship);
	const auto *s = ship.try_get<CombatStructure>();
	if (!s)
		return;
	const auto *destruction = ship.try_get<Destructible>();
	Json output = {{"destroyed", destruction && destruction->destroyed_tick >= 0},
	               {"compartments", Json::array()},
	               {"teams", Json::array()},
	               {"arcs", Json::array()}};
	const bool compartments_visible = address.console == s->definition.repair_console || team_map_allowed(address);
	for (std::size_t i = 0; i < s->compartments.size(); ++i)
		if (compartments_visible)
		{
			const auto &d = s->definition.compartments[i];
			const auto &c = s->compartments[i];
			output["compartments"].push_back(
			    {{"id", d.id},
			     {"index", i + 1},
			     {"deck", d.deck},
			     {"hp", c.hp},
			     {"maximum", d.maximum},
			     {"disabled", c.disabled},
			     {"inspected", repair_team_present(ship, i)},
			     {"damaged", c.hp < d.maximum || std::any_of(
			                                         s->faults.begin(), s->faults.end(),
			                                         [&](const auto &entry)
			                                         {
				                                         return s->definition.equipment_compartments.at(entry.first) ==
				                                                    d.id &&
				                                                std::any_of(entry.second.begin(), entry.second.end(),
				                                                            [](const auto &fault)
				                                                            {
					                                                            return fault.active;
				                                                            });
			                                         })},
			     {"x_mm", d.x},
			     {"y_mm", d.y},
			     {"half_width_mm", d.half_width},
			     {"half_length_mm", d.half_length}});
		}
	const auto &feeds = seat(address.ship, address.console)->feed_systems;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			if (d.kind == OperatingSystem::Team && address.console == s->definition.repair_console)
				output["teams"].push_back({{"id", d.id},
				                           {"location", m->get<TeamState>().location},
				                           {"destination", m->get<TeamState>().destination},
				                           {"travel", m->get<TeamState>().travel},
				                           {"edge_to", m->get<TeamState>().edge_to},
				                           {"edge_total", m->get<TeamState>().edge_total}});
			if (std::find(feeds.begin(), feeds.end(), d.id) == feeds.end())
				continue;
			if ((d.kind == OperatingSystem::Weapon || d.kind == OperatingSystem::Turret) &&
			    std::any_of(definition(address.ship).sensors.begin(), definition(address.ship).sensors.end(),
			                [&](const auto &entry)
			                {
				                return entry.second.capabilities.contains("own_arcs") &&
				                       std::find(feeds.begin(), feeds.end(), entry.first) != feeds.end();
			                }) &&
			    std::none_of(definition(address.ship).sensors.begin(), definition(address.ship).sensors.end(),
			                 [&](const auto &entry)
			                 {
				                 return entry.second.turret == d.id;
			                 }))
			{
				auto heading = ship.get<Pose>().heading +
				               (d.kind == OperatingSystem::Turret ? m->get<TurretState>().heading : d.bearing);
				if (d.kind == OperatingSystem::Weapon && !d.get<WeaponDefinition>().turret.empty())
					heading += instance(ship, d.get<WeaponDefinition>().turret)
					               ->entity.get<CombatMechanism>()
					               .get<TurretState>()
					               .heading;
				output["arcs"].push_back(
				    {{"id", d.id},
				     {"kind", std::string(operating_system_name(d.kind))},
				     {"bearing_mdeg", combat_angle(heading)},
				     {"rest_mdeg", combat_angle(ship.get<Pose>().heading + d.bearing)},
				     {"arc_mdeg", d.arc},
				     {"active", d.kind == OperatingSystem::Turret ? directed_mount_target(ship, child) != 0
				                                                  : directed_target(address.ship, d.id) != 0},
				     {"disabled", structurally_disabled(ship, d.id) || !child.get<Equipment>().enabled}});
			}
		}
	if (combat_duel && duel_ended())
	{
		int alive = 0;
		for (const auto &[id, e] : combat_rows())
			if (e.has<CombatStructure>() && !combat_dead(e))
				++alive;
		output["result"] = alive == 0 ? "Draw" : combat_dead(ship) ? "Defeat" : "Victory";
	}
	output["faults"] = Json::array();
	output["shields"] = Json::array();
	output["weapons"] = Json::array();
	output["magazines"] = Json::array();
	for (const auto &[equipment, defs] : s->definition.faults)
	{
		if (!repair_team_present(ship,
		                         s->definition.compartment_ids.at(s->definition.equipment_compartments.at(equipment))))
			continue;
		if (address.console != s->definition.repair_console &&
		    std::find(feeds.begin(), feeds.end(), equipment) == feeds.end())
			continue;
		for (std::size_t i = 0; i < defs.size(); ++i)
		{
			const auto &state = s->faults.at(equipment)[i];
			const auto &d = defs[i];
			if (!state.active)
				continue;
			output["faults"].push_back(
			    {{"system", equipment},
			     {"compartment", s->definition.equipment_compartments.at(equipment)},
			     {"compartment_index",
			      s->definition.compartment_ids.at(s->definition.equipment_compartments.at(equipment)) + 1},
			     {"symptom", d.symptom},
			     {"diagnosed", state.diagnosed},
			     {"diagnosis", state.diagnosed ? d.diagnosis : std::string{}},
			     {"destroyed", d.destroys},
			     {"work", state.work / (rate * 1000)},
			     {"required_work", d.repair_work}});
		}
	}
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			const auto at = std::find(feeds.begin(), feeds.end(), d.id);
			if (at == feeds.end())
				continue;
			if (d.kind == OperatingSystem::Magazine)
			{
				const auto &stock = physical_stock(ship, m->get<MagazineState>().stock);
				Json choices = Json::array(), selectable = Json::array(), labels = Json::object();
				for (const auto &[resource, profile] : d.get<MagazineDefinition>().ammunition_profiles)
				{
					choices.push_back(resource);
					labels[resource] = stock.catalogue().at(resource).label;
					auto candidate = stock;
					if (candidate.select(resource))
						selectable.push_back(resource);
				}
				output["magazines"].push_back({{"id", d.id},
				                               {"selected_resource", stock.selected()},
				                               {"resource_choices", choices},
				                               {"resource_labels", labels},
				                               {"selectable_resources", selectable},
				                               {"stock_revision", stock.revision()},
				                               {"manual", manual_workload(address, "logistics")}});
			}

			if (d.kind == OperatingSystem::Weapon)
			{
				output["weapons"].push_back(
				    {{"id", d.id},
				     {"magazines", d.get<WeaponDefinition>().magazines},
				     {"selected", m->get<WeaponState>().selected_magazine},
				     {"loaded", m->get<WeaponState>().loaded_magazine},
				     {"loading", m->get<WeaponState>().loading},
				     {"unloading", m->get<WeaponState>().unloading},
				     {"load_ticks", load_duration(ship, *m)},
				     {"unload_ticks", m->get<WeaponState>().loaded_magazine < 0
				                          ? 0
				                          : load_duration(ship, *m, m->get<WeaponState>().loaded_magazine)},
				     {"mode", directed_targeting(address.ship, d.id) == DirectedTargetingKind::Off         ? 0
				              : directed_targeting(address.ship, d.id) == DirectedTargetingKind::Intercept ? 1
				                                                                                           : 2},
				     {"point_defence", d.get<WeaponDefinition>().point_defence},
				     {"intercept", m->get<WeaponState>().intercept}});
				auto &options = output["weapons"].back()["ammunition_options"] = Json::array();
				for (std::size_t i = 0; i < d.get<WeaponDefinition>().magazines.size(); ++i)
				{
					const auto &name = d.get<WeaponDefinition>().magazines[i];
					if (std::find(feeds.begin(), feeds.end(), name) == feeds.end())
						continue;
					const auto &magazine = instance(ship, name)->entity.get<CombatMechanism>();
					const auto &stock = physical_stock(ship, magazine.get<MagazineState>().stock);
					const auto &projectile =
					    stock.selected().empty() || magazine_ammunition(ship, magazine).projectile.visual.empty()
					        ? d.ammunition().projectile
					        : magazine_ammunition(ship, magazine).projectile;
					options.push_back(
					    {{"index", i},
					     {"id", name},
					     {"range_mm", projectile.speed * projectile.lifetime / rate},
					     {"available", stock.available(stock.selected()) >= d.get<WeaponDefinition>().ammo_use ||
					                       m->get<WeaponState>().loaded_magazine == static_cast<std::int64_t>(i)}});
				}
			}
		}
	const auto &seats = definition(address.ship).consoles;
	const auto seat = std::find_if(seats.begin(), seats.end(),
	                               [&](const auto &s)
	                               {
		                               return s.id == address.console;
	                               });
	const bool shield_feed = seat != seats.end() && seat->shield_feed;
	const auto can_control_shield = [&](const std::string &equipment)
	{
		const auto *controls = ship.try_get<EntityControls>();
		if (!controls || equipment.empty())
			return false;
		for (const auto &controller : controls->controllers)
			if (controller.definition.console == address.console)
				for (const auto &command : controller.definition.commands)
					for (const auto index : command.consumers)
						if ((*fittings(ship)).values[index].id == equipment)
							return true;
		return false;
	};
	if (const auto *owner = ship.try_get<ShieldComponent>())
		for (const auto &c : owner->contributions)
			if (shield_feed ||
			    (!c.equipment.empty() && std::find(feeds.begin(), feeds.end(), c.equipment) != feeds.end()))
				output["shields"].push_back(
				    {{"id", c.equipment.empty() ? "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name
				                                : c.equipment},
				     {"contribution", c.definition.name},
				     {"effect_id", c.effect_id},
				     {"component_id", "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name},
				     {"controllable", can_control_shield(c.equipment)},
				     {"strength", c.state.strength},
				     {"capacity", c.definition.shield.reserve.capacity},
				     {"warmup", c.state.warmup},
				     {"warmup_ticks", c.definition.shield.warmup_ticks},
				     {"arc_mdeg", c.definition.arc},
				     {"bearing_mdeg", combat_angle(ship.get<Pose>().heading + c.definition.bearing)},
				     {"layer", c.definition.shield.layer},
				     {"discharging", c.state.discharge}});
	result.gameplay["combat"] = std::move(output);
}
Json save_combat(flecs::entity ship) const
{
	auto result = save_combat_structure(ship.get<CombatStructure>());
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			result["systems"][m->definition.id] = save_combat_mechanism(*m);
	return result;
}
void restore_combat(flecs::entity ship, const Json &j)
{
	auto &s = ship.get_mut<CombatStructure>();
	restore_combat_structure(s, j, combat_dead(ship));
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>())
			restore_combat_mechanism(*m, s.definition, j.at("systems").at(m->definition.id));

	std::map<std::string, std::map<std::string, StockClaim>> reserved;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Weapon)
		{
			const auto &weapon = m->get<WeaponState>();
			if (m->get<WeaponState>().loaded_magazine >= 0)
			{
				if ((!m->get<WeaponState>().unloading &&
				     m->get<WeaponState>().loaded_magazine != m->get<WeaponState>().selected_magazine) ||
				    !load_duration(ship, *m, m->get<WeaponState>().loaded_magazine))
					throw std::runtime_error("Invalid loaded ammunition commitment");
				const auto &magazine_id = m->definition.get<WeaponDefinition>().magazines.at(weapon.loaded_magazine);
				const auto &magazine = instance(ship, magazine_id)->entity.get<CombatMechanism>();
				const auto &stock = physical_stock(ship, magazine.get<MagazineState>().stock);
				const auto &directed = child.get<DirectedRuntime>();
				const auto &modes = directed_definition(ship.get<StableId>().id, m->definition.id)->modes;
				const auto loaded_mode = std::find_if(modes.begin(), modes.end(),
				                                      [&](const auto &mode)
				                                      {
					                                      return mode.id == directed.loaded_mode;
				                                      });
				if (loaded_mode == modes.end() || loaded_mode->operation != DirectedOperationKind::Launch ||
				    weapon.loaded_resource.empty() || weapon.loaded_resource != stock.selected() ||
				    !magazine.definition.get<MagazineDefinition>().ammunition_profiles.contains(
				        weapon.loaded_resource) ||
				    weapon.loaded_quantity != loaded_mode->ammo_use ||
				    (directed_operation(ship.get<StableId>().id, m->definition.id, DirectedOperationKind::Launch) &&
				     !weapon.unloading && weapon.loaded_quantity != m->definition.get<WeaponDefinition>().ammo_use))
					throw std::runtime_error("Invalid loaded resource identity");
				reserved[magazine_id].emplace("weapon:" + m->definition.id,
				                              StockClaim{weapon.loaded_resource, weapon.loaded_quantity});
			}
			else if (!weapon.loaded_resource.empty() || weapon.loaded_quantity)
				throw std::runtime_error("Orphan loaded resource identity");
			if (m->get<WeaponState>().unloading < 0 ||
			    (m->get<WeaponState>().unloading &&
			     (m->get<WeaponState>().loaded_magazine < 0 ||
			      m->get<WeaponState>().unloading > load_duration(ship, *m, m->get<WeaponState>().loaded_magazine) ||
			      m->get<WeaponState>().loading)))
				throw std::runtime_error("Invalid unload progress");
			if (m->get<WeaponState>().loading < 0 ||
			    (load_duration(ship, *m) > 0 ? m->get<WeaponState>().loading >= load_duration(ship, *m)
			                                 : m->get<WeaponState>().loading != 0) ||
			    m->get<WeaponState>().bypass_remainder < 0 || m->get<WeaponState>().bypass_remainder >= 1000)
				throw std::runtime_error("Invalid weapon progress");
		}
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Magazine)
		{
			std::map<std::string, StockClaim> actual;
			for (const auto &[key, claim] : physical_stock(ship, m->get<MagazineState>().stock).capacity_claims())
				if (key.starts_with("weapon:"))
					actual.emplace(key, claim);
			if (actual != reserved[m->definition.id])
				throw std::runtime_error("Unbalanced magazine return reservation");
		}
	if (combat_dead(ship))
		ship.remove<Movable>();
}
Json save_projectiles() const
{
	Json result = Json::array();
	for (auto particle : projectile_rows())
		result.push_back(save_projectile_continuation(particle.get<Projectile>(), particle.get<Destructible>()));
	return result;
}
void restore_projectiles(const Json &saved)
{
	OrdnanceRestoreCatalogue catalogue;
	for (const auto &[id, definition] : definitions)
		catalogue.visuals.insert(id);
	for (const auto &[id, payload] : payload_templates)
		catalogue.payloads.insert(id);
	for (const auto &[id, entity] : entities)
		if (const auto *structure = entity.try_get<CombatStructure>())
			for (const auto &compartment : structure->definition.compartments)
				catalogue.decks[id].insert(compartment.deck);
	auto restored =
	    restore_projectile_continuations(saved, static_cast<std::int64_t>(ecs.get<Clock>().tick), catalogue);
	next_projectile = restored.next_projectile;
	for (auto &value : restored.values)
	{
		validate_rule_captures(value.projectile.rule_captures, value.projectile.payloads);
		validate_rule_captures(value.projectile.destruction_rule_captures,
		                       value.projectile.definition.destruction.payloads);
		ecs.entity().set<Projectile>(value.projectile).set<Destructible>(std::move(value.native));
	}
}

Json save_shields(flecs::entity ship) const
{
	return save_shield_continuation(ship.get<ShieldComponent>());
}
void restore_shields(flecs::entity ship, const Json &value)
{
	CombatMechanismDefinitions equipment;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			equipment.emplace(m->definition.id, m->definition);
	ship.set<ShieldComponent>(restore_shield_continuation(value, equipment, shield_templates));
}
