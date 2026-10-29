module;
#include "native_headers.hpp"

module phoenix.simulation;
import :crew_bindings;
import :away_adapters;

namespace phoenix::world_detail
{
auto Runtime::active_displacement_transfer(EntityId id) const -> bool
{
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &mission = away_operations.missions()[i];
		const auto &mission_definition = away_operations.definitions()[i];
		const auto origin = away_ship(mission_definition);
		if (!origin || (origin != id && mission_definition.target != id))
			continue;
		if (!definition(origin).displacements.contains(mission.transport) || mission.transfer <= 0)
			continue;
		if (mission.phase == AwayPhase::Outbound || mission.phase == AwayPhase::Inbound)
			return true;
	}

	return false;
}

auto Runtime::away_ship(const AwayDefinition &d) const -> EntityId
{
	return away_operations.origin(d, slot_entities);
}

auto Runtime::away_transport_entity(EntityId ship, const std::string &transport) const -> EntityId
{
	if (transport == "ship")
		return ship;
	if (transport.starts_with("craft:"))
		for (const auto &[id, entity] : entities)
			if (transport == "craft:" + std::to_string(id) && entity.has<CraftState>() &&
			    entity.get<CraftState>().home == ship)
				return id;
	return 0;
}

auto Runtime::canonical_away_transport(EntityId ship, const std::string &transport, const std::string &mission) const
    -> std::string
{
	if (const auto bay = definition(ship).craft_bays.find(transport);
	    bay != definition(ship).craft_bays.end() && bay->second.legacy_transport)
		for (const auto &[id, e] : craft_rows())
			if (e.get<CraftState>().home == ship && e.get<CraftState>().bay == transport &&
			    craft_available(id, mission))
				return "craft:" + std::to_string(id);
	return transport;
}

auto Runtime::away_ship_transport_owned(const ConsoleAddress &a) const -> bool
{
	return away_dispatch(a);
}

auto Runtime::away_conversation(const AwayDefinition &d) const -> ConversationId
{
	if (!d.hail_tree.empty())
	{
		const auto ship = "ship:" + std::to_string(away_ship(d));
		const auto target = "ship:" + std::to_string(d.target);
		for (const auto &[id, conversation] : conversations.conversations)
			if (conversation.program == d.hail_tree && conversation.members.contains(ship))
				for (const auto &member : conversation.members)
					if (member == target || member.starts_with(target + "/"))
						return id;
		return 0;
	}
	for (const auto &c : conversation_starts)
		if (c.id == d.conversation)
			return c.conversation;
	return 0;
}

auto Runtime::away_equipment(EntityId ship, const std::string &key) const -> flecs::entity
{
	for (auto e : entities.at(ship).get<EquipmentIndex>().values)
		if (e.get<Equipment>().id == key)
			return e;
	return {};
}

auto Runtime::away_team_duty(EntityId ship, const std::string &key) const -> const PersonnelDuty *
{
	for (const auto &d : definition(ship).personnel.duties)
		if (d.team == key)
			return &d;
	return nullptr;
}

auto Runtime::away_officer_location(EntityId ship, const std::string &officer) const -> std::string
{
	if (personnel_postings.contains(ship) && personnel_postings.at(ship).contains("officer:" + officer))
		return personnel_postings.at(ship).at("officer:" + officer).status;
	if (mission_ledger.lost.contains(ship) && mission_ledger.lost.at(ship).contains("officer:" + officer))
		return "Lost";
	return away_operations.officer_away(ship, officer, slot_entities) ? "Away" : std::string{};
}

auto Runtime::away_reserved(EntityId ship, const std::string &asset) const -> bool
{
	return away_operations.reserved(ship, asset, {slot_entities, personnel_postings, mission_ledger});
}

auto Runtime::away_transport_requested(EntityId ship, const std::string &system) const -> bool
{
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &m = away_operations.missions()[i];
		if (away_ship(away_operations.definitions()[i]) != ship ||
		    (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
			continue;
		const auto craft = away_transport_entity(ship, m.transport);
		if ((m.transport == system || (craft && craft != ship && entities.at(craft).get<CraftState>().bay == system)) &&
		    away_blocker(i, false).empty())
			return true;
	}
	return false;
}

auto Runtime::away_craft_reserved(EntityId craft, const std::string &except) const -> bool
{
	const auto &state = entities.at(craft).get<CraftState>();
	return away_operations.craft_reserved(state.home, craft, except, slot_entities);
}

auto Runtime::return_away_escorts(AwayMission &m) -> void
{
	for (const auto &[slot, id] : m.escorts)
		if (entities.contains(id) && entities.at(id).get<CraftState>().phase != CraftPhase::Lost)
			entities.at(id).get_mut<CraftState>().pending_order = "return";
}

auto Runtime::away_qualified(EntityId ship, const std::string &system) const -> bool
{
	const auto owner = enabled_system_owner(ship, system);
	const auto &resolved = definition(ship);
	const auto &personnel = entities.at(ship).get<PersonnelState>();
	return away_navigator_qualified(resolved.personnel, personnel, resolved.layout ? &*resolved.layout : nullptr,
	                                resolved.fitted ? &*resolved.fitted : nullptr, owner.console, system);
}

auto Runtime::away_dispatch_system(EntityId ship, const std::string &route) const -> std::string
{
	if (!route.empty())
		return route;
	if (const auto dispatch = away_equipment(ship, "dispatch");
	    dispatch && dispatch.get<Equipment>().kind == OperatingSystem::Dispatch)
		return "dispatch";
	std::string unique;
	for (const auto equipment : entities.at(ship).get<EquipmentIndex>().values)
		if (equipment.get<Equipment>().kind == OperatingSystem::Dispatch)
		{
			if (!unique.empty())
				return {};
			unique = equipment.get<Equipment>().id;
		}
	return unique;
}

auto Runtime::away_dispatch(const ConsoleAddress &a, const std::string &route) const -> bool
{
	if (combat_dead(entities.at(a.ship)))
		return false;
	for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
		if (e.get<Equipment>().kind == OperatingSystem::Dispatch && e.get<Equipment>().enabled &&
		    e.get<Equipment>().id == away_dispatch_system(a.ship, route) &&
		    enabled_system_owner(a.ship, e.get<Equipment>().id) == a)
			return true;
	return false;
}

auto Runtime::away_asset_owned(const ConsoleAddress &a, const std::string &asset) const -> bool
{
	if (asset.starts_with("craft:"))
	{
		const auto craft = away_transport_entity(a.ship, asset);
		return craft && craft != a.ship && enabled_system_owner(a.ship, entities.at(craft).get<CraftState>().bay) == a;
	}
	if (asset.starts_with("team:"))
	{
		const auto *d = away_team_duty(a.ship, asset.substr(5));
		return d && personnel_owns(a, d->department);
	}
	if (asset.starts_with("officer:"))
		for (const auto &o : definition(a.ship).personnel.officers)
			if (o.id == asset.substr(8))
				return personnel_owns(a, o.department);
	if (asset.starts_with("transport:"))
	{
		if (asset == "transport:ship")
			return away_ship_transport_owned(a);
		if (const auto craft = away_transport_entity(a.ship, asset.substr(10)); craft && craft != a.ship)
			return enabled_system_owner(a.ship, entities.at(craft).get<CraftState>().bay) == a;
		return (definition(a.ship).displacements.contains(asset.substr(10)) ||
		        definition(a.ship).craft_bays.contains(asset.substr(10))) &&
		       enabled_system_owner(a.ship, asset.substr(10)) == a;
	}
	return false;
}

auto Runtime::away_has_capability(EntityId ship, const AwayMission &m, const std::string &capability) const -> bool
{
	if (!m.deployed())
		return false;
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
			if (const auto team = away_equipment(ship, asset.substr(5));
			    team && team.get<Equipment>().enabled && !structurally_disabled(entities.at(ship), asset.substr(5)) &&
			    !team.get<CombatMechanism>().get<TeamState>().detained &&
			    team.get<CombatMechanism>().get<TeamState>().condition != "lost" &&
			    team.get<CombatMechanism>().definition.get<TeamDefinition>().capabilities.contains(capability))
			{
				const auto &properties = modifier_properties();
				if (std::any_of(properties.begin(), properties.end(),
				                [&](const auto &p)
				                {
					                return p.id == capability;
				                }))
				{
					if (team_work(entities.at(ship), team.get<CombatMechanism>(), property_key(capability), 1000) > 0)
						return true;
				}
				else if (team.get<CombatMechanism>().get<TeamState>().condition != "injured" ||
				         team.get<CombatMechanism>().definition.get<TeamDefinition>().injured_effectiveness > 0)
					return true;
			}
	return false;
}

auto Runtime::away_asset_healthy(EntityId ship, const std::string &asset) const -> bool
{
	const auto &definition_personnel = definition(ship).personnel;
	const auto &personnel = entities.at(ship).get<PersonnelState>();
	bool available = false;
	const PersonnelDuty *duty = nullptr;
	if (asset.starts_with("team:"))
	{
		if (const auto team = away_equipment(ship, asset.substr(5)); team)
			available = team_available(team.get<CombatMechanism>());
		duty = away_team_duty(ship, asset.substr(5));
	}
	return away_crew_healthy(definition_personnel, personnel, asset, available, duty);
}

auto Runtime::away_slot_accepts(EntityId ship, const AwaySlot &s, const std::string &asset) const -> bool
{
	NativeAwayReadBinding binding(*this);
	return away_operations.slot_accepts(ship, s, asset, binding.observation);
}

auto Runtime::away_origin(EntityId ship, const std::string &transport) const -> std::size_t
{
	NativeAwayReadBinding binding(*this);
	return away_operations.staging_location(ship, transport, binding.observation);
}

auto Runtime::away_staged(EntityId ship, const AwayMission &m) const -> bool
{
	NativeAwayReadBinding binding(*this);
	return away_operations.staged(ship, m, binding.observation);
}

auto Runtime::away_transfer_ticks(const AwayDefinition &d, const AwayMission &m) const -> std::int64_t
{
	NativeAwayReadBinding binding(*this);
	return away_operations.transfer_ticks(d, m, binding.observation);
}

auto Runtime::mark_away_deployed(const AwayDefinition &definition, AwayMission &m) -> void
{
	NativeAwayReadBinding binding(*this);
	away_operations.deploy(definition, m, binding.observation);
}

auto Runtime::finish_away_recovery(EntityId ship, AwayMission &m) -> void
{
	NativeAwayBinding binding(*this);
	away_operations.recover(ship, m, binding.execution);
}

auto Runtime::boarding_insertion_blocker(std::size_t index, const std::string &transport) const -> std::string
{
	const auto &d = away_operations.definitions()[index];
	const auto ship = away_ship(d);
	const auto physical = away_transport_entity(ship, transport);
	if (physical)
	{
		if (physical != ship && definition(physical).craft->role == "boarding_torpedo")
			return {};
		if (!d.boarding)
			return {};
		const auto &structure = entities.at(d.target).get<CombatStructure>();
		std::int64_t hp = 0, maximum = 0;
		for (std::size_t i = 0; i < structure.compartments.size(); ++i)
		{
			hp += structure.compartments[i].hp;
			maximum += structure.definition.compartments[i].maximum;
		}
		return hp * 2 >= maximum ? "Boarding by shuttle or docking requires hull below 50%" : "";
	}
	for (auto id : {ship, d.target})
		if (const auto *owner = entities.at(id).try_get<ShieldComponent>())
			for (const auto &c : owner->contributions)
				if (shield_available(entities.at(id), c) && c.state.warmup > 0 && c.state.strength > 0)
				{
					const auto &pose = entities.at(id).get<Pose>();
					const auto other = entities.at(id == ship ? d.target : ship).get<Pose>().position;
					const auto bearing = combat_bearing(other - pose.position);
					if (std::abs(combat_delta(bearing - pose.heading, c.definition.bearing)) <= c.definition.arc / 2)
						return "Lower shields for displacement";
				}

	return {};
}

auto Runtime::away_blocker(std::size_t index, bool power) const -> std::string
{
	const auto &d = away_operations.definitions()[index];
	const auto &m = away_operations.missions()[index];
	const auto ship = away_ship(d);
	if (!ship || !entities.contains(d.target))
		return "Destination unavailable";
	if (m.transport.empty())
		return "Transport required";
	if ((m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Outbound))
		if (const auto blocker = boarding_insertion_blocker(index, m.transport); !blocker.empty())
			return blocker;
	if (const auto transport = away_transport_entity(ship, m.transport))
	{
		if (combat_dead(entities.at(ship)) || combat_dead(entities.at(d.target)) || combat_dead(entities.at(transport)))
			return "Destination unavailable";
		if (transport != ship)
		{
			const auto &state = entities.at(transport).get<CraftState>();
			if (state.phase == CraftPhase::Lost || state.phase == CraftPhase::Consumed)
				return "Transport unavailable";
			if (m.phase == AwayPhase::Deployed && definition(transport).craft->role == "boarding_torpedo")
				return "Boarding torpedo cannot recover personnel";
			const auto &bay = definition(ship).craft_bays.at(state.bay);
			if (!bay_working(ship, state.bay))
				return "Transport disabled";
			if (bay.legacy_transport && m.phase != AwayPhase::Inbound &&
			    distance_3d(ship, d.target) > bay.transport_range)
				return "Out of transport range";
		}
		if (transport == ship && !d.allow_ship_transport)
			return d.ship_transport_reason.empty() ? "Ship transport disabled by mission" : d.ship_transport_reason;
		if (m.assignments.size() + m.accompanying.size() >
		    static_cast<std::size_t>(definition(transport).transport.capacity))
			return "Transport capacity exceeded";
		if (transport == ship && !away_dock_ready(ship, d, m.transfer > 0))
			return "Dock at a mission-compatible port";
		return {};
	}
	const auto &t = definition(ship).displacements.at(m.transport);
	if (fixed::length(entities.at(ship).get<Pose>().position - entities.at(d.target).get<Pose>().position) > t.range)
		return "Out of displacement range";
	if (combat_dead(entities.at(ship)) || combat_dead(entities.at(d.target)))
		return "Destination unavailable";
	const auto equipment = away_equipment(ship, m.transport);
	if (!equipment.get<Equipment>().enabled || structurally_disabled(entities.at(ship), m.transport) ||
	    !fault_effectiveness(entities.at(ship), m.transport))
		return "Displacement disabled";
	if (!away_qualified(ship, m.transport))
		return "Qualified navigator required";
	if (m.assignments.size() + m.accompanying.size() > static_cast<std::size_t>(t.capacity))
		return "Displacement capacity exceeded";
	if (power)
	{
		const auto *p = entities.at(ship).try_get<ShipResources>();
		if (!p || !equipment.has<ResourceBinding>() ||
		    p->grants.at(equipment.get<ResourceBinding>().slot).supply_permille != 1000)
			return "Insufficient displacement power";
	}
	return {};
}

auto Runtime::prepare_away_officers(const ConsoleAddress &address, std::size_t index) -> void
{
	const auto *staffing = native_operation_controller(address, "mission_staffing");
	if (staffing ? controller_operation_human(*staffing, "mission_staffing")
	             : station_stars(address) >= 3 && console_operator(address) != 0)
		return;
	const auto &d = away_operations.definitions().at(index);
	for (const auto &slot : d.slots)
		if (slot.kind == "officer" && !away_operations.mission(index).assignments.contains(slot.id))
			for (const auto &officer : definition(address.ship).personnel.officers)
			{
				const auto asset = "officer:" + officer.id;
				if (!away_asset_owned(address, asset) || away_reserved(address.ship, asset) ||
				    !away_asset_healthy(address.ship, asset) || !away_slot_accepts(address.ship, slot, asset))
					continue;
				bool assigned = false;
				for (const auto &controller : entities.at(address.ship).get<EntityControls>().controllers)
					if (controller.definition.console == address.console)
					{
						OperatingCommand command{address.ship, "away.assign",
						                         static_cast<std::int64_t>(away_operations.mission(index).revision),
						                         d.id};
						command.text = slot.id + "/" + asset;
						command.controller = controller.definition.id;
						command.generation = controller.generation;
						if (away_command(address, command, true,
						                 controller.definition.widget.empty() ? !controller.human : true, true))
						{
							assigned = true;
							break;
						}
					}
				if (assigned)
					break;
			}
}

auto Runtime::prepare_away_contribution(const ConsoleAddress &a, std::size_t index) -> void
{
	auto &m = away_operations.mission(index);
	const auto &d = away_operations.definitions().at(index);
	if (!m.authorised_departments.contains(a.console) ||
	    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return;
	const bool native_transport = native_operation_controller(a, "transport") != nullptr;
	if ((native_transport || station_stars(a) < 2 || console_operator(a) == 0) && m.transport.empty())
		for (const auto &[id, e] : craft_rows())
			if (e.get<CraftState>().home == a.ship && definition(id).craft->role == "shuttle" &&
			    away_asset_owned(a, "craft:" + std::to_string(id)) && craft_available(id))
				for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
					if (ctrl.definition.console == a.console && m.transport.empty())
					{
						OperatingCommand c{a.ship, "away.transport", static_cast<std::int64_t>(m.revision), d.id};
						c.controller = ctrl.definition.id;
						c.generation = ctrl.generation;
						c.text = "craft:" + std::to_string(id);
						away_command(a, c, true, ctrl.definition.widget.empty() ? !ctrl.human : true, true);
					}
	if (native_transport || station_stars(a) < 3 || console_operator(a) == 0)
		for (const auto &slot : d.slots)
			if (slot.kind == "team" && !m.assignments.contains(slot.id))
				for (const auto &duty : definition(a.ship).personnel.duties)
					if (!duty.team.empty() && away_asset_owned(a, "team:" + duty.team))
						for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
							if (ctrl.definition.console == a.console && !m.assignments.contains(slot.id))
							{
								OperatingCommand c{a.ship, "away.assign", static_cast<std::int64_t>(m.revision), d.id};
								c.controller = ctrl.definition.id;
								c.generation = ctrl.generation;
								c.text = slot.id + "/team:" + duty.team;
								away_command(a, c, true, ctrl.definition.widget.empty() ? !ctrl.human : true, true);
							}
	if (native_transport || station_stars(a) < 2 || console_operator(a) == 0)
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort" && !m.escorts.contains(slot.id))
				for (const auto &[id, e] : craft_rows())
					if (e.get<CraftState>().home == a.ship && definition(id).craft->role == slot.craft_role &&
					    away_asset_owned(a, "craft:" + std::to_string(id)) && craft_available(id))
						for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
							if (ctrl.definition.console == a.console && !m.escorts.contains(slot.id))
							{
								OperatingCommand c{a.ship, "away.escort", static_cast<std::int64_t>(m.revision), d.id};
								c.controller = ctrl.definition.id;
								c.generation = ctrl.generation;
								c.text = slot.id + "/craft:" + std::to_string(id);
								away_command(a, c, true, ctrl.definition.widget.empty() ? !ctrl.human : true, true);
							}
	prepare_away_officers(a, index);
}
} // namespace phoenix::world_detail
