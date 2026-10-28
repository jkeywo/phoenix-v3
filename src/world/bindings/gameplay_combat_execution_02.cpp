module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::project_combat(const ConsoleAddress &address, Observation &result) const -> void
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

auto Runtime::save_combat(flecs::entity ship) const -> Json
{
	auto result = save_combat_structure(ship.get<CombatStructure>());
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			result["systems"][m->definition.id] = save_combat_mechanism(*m);
	return result;
}

auto Runtime::restore_combat(flecs::entity ship, const Json &j) -> void
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

auto Runtime::save_projectiles() const -> Json
{
	return projectiles.save();
}

auto Runtime::restore_projectiles(const Json &saved) -> void
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
	projectiles.restore(ecs, saved, static_cast<std::int64_t>(ecs.get<Clock>().tick), catalogue,
	                    [this](const Projectile &p)
	                    {
		                    validate_rule_captures(p.rule_captures, p.payloads);
		                    validate_rule_captures(p.destruction_rule_captures, p.definition.destruction.payloads);
	                    });
}

auto Runtime::save_shields(flecs::entity ship) const -> Json
{
	return save_shield_continuation(ship.get<ShieldComponent>());
}

auto Runtime::restore_shields(flecs::entity ship, const Json &value) -> void
{
	CombatMechanismDefinitions equipment;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
			equipment.emplace(m->definition.id, m->definition);
	ship.set<ShieldComponent>(restore_shield_continuation(value, equipment, shield_templates));
}
} // namespace phoenix::world_detail
