module;
#include "native_headers.hpp"

module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::altitude(EntityId id) const -> std::int64_t
{
	const auto *v = entities.at(id).try_get<VerticalMotion>();
	return v ? v->altitude : entities.at(id).get<Pose>().altitude;
}

auto Runtime::craft_flight_band(EntityId id) const -> std::int64_t
{
	const auto *state = entities.at(id).try_get<CraftState>();
	if (!state)
		return 0;
	const auto &bay = definition(state->home).craft_bays.at(state->bay);
	// Lower slot indices use higher bands for the bay's stable serial launch order.
	return bay.flight_spacing * (bay.slots - state->slot);
}

auto Runtime::distance_3d(EntityId a, EntityId b) const -> std::int64_t
{
	const auto xy = entities.at(a).get<Pose>().position - entities.at(b).get<Pose>().position;
	const auto z = altitude(a) - altitude(b);
	return static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(xy, xy) + z * z)));
}

auto Runtime::craft_rows() const -> std::vector<std::pair<EntityId, flecs::entity>>
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	craft_query.each(
	    [&](flecs::entity e, CraftState &)
	    {
		    rows.emplace_back(e.get<StableId>().id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}

auto Runtime::craft_dormant(flecs::entity e) const -> bool
{
	const auto *c = e.try_get<CraftState>();
	return c && craft_inactive(*c);
}

auto Runtime::bay_working(EntityId ship, const std::string &bay) const -> bool
{
	if (!entities.contains(ship) || departed(ship) || combat_dead(entities.at(ship)))
		return false;
	const auto equipment = away_equipment(ship, bay);
	return equipment && equipment.get<Equipment>().enabled && !structurally_disabled(entities.at(ship), bay) &&
	       fault_effectiveness(entities.at(ship), bay) > 0;
}

auto Runtime::craft_docking_ready(EntityId id) const -> bool
{
	const auto e = entities.at(id);
	const auto &s = e.get<CraftState>();
	const auto &bay = definition(s.home).craft_bays.at(s.bay);
	return bay_working(s.home, s.bay) && distance_3d(id, s.home) <= bay.docking_range &&
	       (!bay.legacy_transport ||
	        fixed::length(e.get<Pose>().position - entities.at(s.home).get<Pose>().position) <= bay.arrival_distance) &&
	       fixed::length(e.get<Motion>().velocity - entities.at(s.home).get<Motion>().velocity) <= bay.docking_speed;
}

auto Runtime::ftl_preparing(EntityId ship) const -> bool
{
	if (!entities.contains(ship))
		return false;
	for (auto e : entities.at(ship).get<EquipmentIndex>().values)
		if (e.has<FtlReserve>() && e.get<Equipment>().enabled)
			return true;
	return false;
}

auto Runtime::ftl_recall_started(EntityId ship) const -> bool
{
	if (!entities.contains(ship) || departed(ship) || combat_dead(entities.at(ship)))
		return false;
	const auto e = entities.at(ship);
	const auto *power = e.try_get<ShipResources>();
	if (!power)
		return false;
	for (auto child : e.get<EquipmentIndex>().values)
		if (const auto *drive = child.try_get<FtlReserve>(); drive && child.get<Equipment>().enabled &&
		                                                     !structurally_disabled(e, child.get<Equipment>().id) &&
		                                                     fault_effectiveness(e, child.get<Equipment>().id) > 0)
		{
			const auto &store = power->network.stores().at(drive->definition.store);
			if (store.enabled && !store.thermal_stop &&
			    power->grants.at(child.get<ResourceBinding>().slot).supply_permille > 0)
				return true;
		}
	return false;
}

auto Runtime::craft_link(EntityId craft) const -> bool
{
	const auto &s = entities.at(craft).get<CraftState>();
	if (craft_dormant(entities.at(craft)))
		return entities.contains(s.home) && !departed(s.home) && !combat_dead(entities.at(s.home));
	if (!entities.contains(s.home) || departed(s.home) || combat_dead(entities.at(s.home)))
		return false;
	const auto a = conversations.endpoints.find(craft), b = conversations.endpoints.find(s.home);
	return a != conversations.endpoints.end() && b != conversations.endpoints.end() &&
	       comms_endpoints_linked(a->second, b->second);
}

auto Runtime::set_craft_active(flecs::entity e, bool active) -> void
{
	if (active)
		e.enable();
	else
		e.disable();
	for (auto child : e.get<EquipmentIndex>().values)
		if (active)
			child.enable();
		else
			child.disable();
}

auto Runtime::initialise_craft(std::vector<EntityId> *created) -> void
{
	std::vector<EntityId> carriers;
	for (const auto &[id, e] : entities)
		if (!definition(id).craft_bays.empty())
			carriers.push_back(id);
	for (auto home : carriers)
		for (const auto &[bay, d] : definition(home).craft_bays)
		{
			if (away_equipment(home, bay).has<CraftBayState>())
				continue;
			CraftBayState state;
			state.ammunition_remainders.resize(d.ammunition.size());
			away_equipment(home, bay).set<CraftBayState>(state);
			for (std::size_t slot = 0; slot < d.initial.size(); ++slot)
			{
				const auto id = next_entity;
				if (id >= 1000000000ULL)
					throw std::runtime_error("Carried craft identity exhausted");
				const auto &p = entities.at(home).get<Pose>();
				create(id, d.initial[slot],
				       {{"x_mm", p.position.x}, {"y_mm", p.position.y}, {"heading_mdeg", p.heading}});
				if (created)
					created->push_back(id);
				CraftState c;
				c.home = home;
				c.bay = bay;
				c.slot = slot;
				c.rendezvous = p.position;
				c.rendezvous_velocity = entities.at(home).get<Motion>().velocity;
				c.rendezvous_altitude = altitude(home);
				entities.at(id).set<CraftState>(c);
				if (const auto *f = entities.at(home).try_get<FactionMembership>())
					entities.at(id).set<FactionMembership>(*f);
				set_craft_active(entities.at(id), false);
			}
		}
}

auto Runtime::restore_spawned_craft(const Json &saved) -> void
{
	if (saved.is_null())
		return;
	std::vector<EntityId> carriers;
	for (const auto &[id, e] : entities)
		if (!definition(id).craft_bays.empty())
			carriers.push_back(id);
	for (auto home : carriers)
		for (const auto &[bay, d] : definition(home).craft_bays)
		{
			if (away_equipment(home, bay).has<CraftBayState>())
				continue;
			CraftBayState state;
			state.ammunition_remainders.resize(d.ammunition.size());
			away_equipment(home, bay).set<CraftBayState>(state);
			for (std::size_t slot = 0; slot < d.initial.size(); ++slot)
			{
				EntityId id = 0;
				for (const auto &row : saved.at("entities"))
					if (row.at("state").at("home") == home && row.at("state").at("bay") == bay &&
					    row.at("state").at("slot") == slot)
					{
						if (id)
							throw std::runtime_error("Duplicate saved carried slot");
						id = row.at("id").get<EntityId>();
					}
				if (!id || id >= 1000000000ULL || entities.contains(id))
					throw std::runtime_error("Missing or overlapping saved carried identity");
				create(id, d.initial[slot], {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
				CraftState c;
				c.home = home;
				c.bay = bay;
				c.slot = slot;
				entities.at(id).set<CraftState>(c);
				set_craft_active(entities.at(id), false);
			}
		}
}

auto Runtime::torpedo_launched(flecs::entity e) const -> bool
{
	const auto &s = e.get<CraftState>();
	return definition(e.get<StableId>().id).craft->role == "boarding_torpedo" && s.phase != CraftPhase::Docked &&
	       s.phase != CraftPhase::Launching;
}

auto Runtime::release_away_transport(EntityId ship, AwayMission &m) -> void
{
	const auto id = away_transport_entity(ship, m.transport);
	if (id && id != ship)
	{
		auto e = entities.at(id);
		auto &s = e.get_mut<CraftState>();
		s.mission.clear();
		if (s.phase != CraftPhase::Consumed && s.phase != CraftPhase::Lost)
			craft_return(e);
	}
	m.transport_released = false;
}

auto Runtime::craft_available(EntityId id, const std::string &mission) const -> bool
{
	const auto &s = entities.at(id).get<CraftState>();
	return !cargo_committed(id) && !combat_dead(entities.at(id)) && s.phase == CraftPhase::Docked &&
	       !away_craft_reserved(id, mission) && s.order == "idle" && s.pending_order.empty() &&
	       (s.mission.empty() || s.mission == mission) && bay_working(s.home, s.bay) && !ftl_preparing(s.home);
}

auto Runtime::craft_return(flecs::entity e) -> void
{
	auto &c = e.get_mut<CraftState>();
	if (torpedo_launched(e))
		return;
	if (c.phase == CraftPhase::Docked || c.phase == CraftPhase::Launching)
	{
		c.phase = CraftPhase::Docked;
		c.order = "idle";
		c.progress = 0;
	}
	else if (c.phase != CraftPhase::Lost)
		c.order = "return";
	c.pending_order.clear();
	c.pending_target = 0;
	++c.revision;
}

auto Runtime::complete_craft_recovery(EntityId craft) -> void
{
	auto &s = entities.at(craft).get_mut<CraftState>();
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		auto &m = away_operations.missions()[i];
		if (away_operations.definitions()[i].id != s.mission || away_ship(away_operations.definitions()[i]) != s.home ||
		    !m.aboard_craft || m.transport != "craft:" + std::to_string(craft) || m.supply_job)
			continue;
		finish_away_recovery(s.home, m);
	}
	for (auto &mission : away_operations.missions())
		if (std::erase_if(mission.escorts,
		                  [&](const auto &slot)
		                  {
			                  return slot.second == craft;
		                  }))
			++mission.revision;
	s.mission.clear();
}

auto Runtime::request_craft_pickup(EntityId craft) -> void
{
	auto e = entities.at(craft);
	auto &s = e.get_mut<CraftState>();
	if (torpedo_launched(e))
		return;
	for (auto &m : away_operations.missions())
		for (auto at = m.escorts.begin(); at != m.escorts.end(); ++at)
			if (at->second == craft)
			{
				m.escorts.erase(at);
				++m.revision;
				s.mission.clear();
				craft_return(e);
				return;
			}
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		auto &m = away_operations.missions()[i];
		const auto &d = away_operations.definitions()[i];
		if (away_ship(d) != s.home || m.transport != "craft:" + std::to_string(craft) ||
		    (!s.mission.empty() && s.mission != d.id) ||
		    (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Deployed && m.phase != AwayPhase::Inbound))
			continue;
		s.mission = d.id;
		m.phase = AwayPhase::Inbound;
		m.transport_released = false;
		m.transfer = 0;
		++m.revision;
		s.pending_order.clear();
		if (m.aboard_craft)
		{
			if (s.phase == CraftPhase::Launching || s.phase == CraftPhase::Docked)
			{
				s.phase = CraftPhase::Docked;
				s.order = "idle";
				complete_craft_recovery(craft);
			}
			else
				craft_return(e);
		}
		else
		{
			s.order = "pickup";
			s.target = d.target;
			s.target_position = entities.at(d.target).get<Pose>().position;
			s.target_velocity = entities.at(d.target).get<Motion>().velocity;
			s.target_altitude = altitude(d.target);
			if (s.phase == CraftPhase::Docked)
				launch_craft(e);
		}
		return;
	}
	craft_return(e);
}

auto Runtime::advance_craft_transport(std::size_t index, EntityId craft) -> void
{
	auto &m = away_operations.missions()[index];
	const auto &d = away_operations.definitions()[index];
	auto e = entities.at(craft);
	auto &s = e.get_mut<CraftState>();
	if ((m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound) || s.mission != d.id ||
	    s.phase != CraftPhase::Flying || combat_dead(e) || combat_dead(entities.at(d.target)))
		return;
	const bool delivering = m.phase == AwayPhase::Outbound && m.aboard_craft;
	const bool collecting = m.phase == AwayPhase::Inbound && !m.aboard_craft;
	const auto &bay = definition(s.home).craft_bays.at(s.bay);
	bool arrived = distance_3d(craft, d.target) <= d.deployment_range;
	if (bay.legacy_transport)
	{
		const auto clearance = definition(d.target).half_height + definition(craft).half_height + 1000;
		const auto vertical = altitude(craft) - altitude(d.target) - clearance;
		const auto velocity = e.get<Motion>().velocity - entities.at(d.target).get<Motion>().velocity;
		const auto vertical_speed =
		    e.get<Motion>().vertical_velocity - entities.at(d.target).get<Motion>().vertical_velocity;
		arrived =
		    fixed::length(e.get<Pose>().position - entities.at(d.target).get<Pose>().position) <=
		        bay.arrival_distance &&
		    std::abs(vertical) <= bay.arrival_distance &&
		    fixed::dot(velocity, velocity) + vertical_speed * vertical_speed <= bay.arrival_speed * bay.arrival_speed;
	}
	if ((!delivering && !collecting) || !arrived || (delivering && !away_blocker(index, false).empty()))
		return;
	if (++m.transfer < away_transfer_ticks(d, m))
		return;
	m.transfer = 0;
	if (collecting)
	{
		m.aboard_craft = true;
		++m.revision;
		craft_return(e);
	}
	else
	{
		m.aboard_craft = false;
		mark_away_deployed(d, m);
		if (definition(craft).craft->role == "boarding_torpedo")
		{
			s.phase = CraftPhase::Consumed;
			s.order = "idle";
			s.mission.clear();
			s.pending_order.clear();
			s.pending_target = 0;
			s.progress = 0;
			m.transport_released = true;
			// A consumed craft cannot launch again. Close its native executions
			// before disabling the physical owner, just as for a departed ship.
			retire_ship_processes(craft);
			retire_process_dependants();
			set_craft_active(e, false);
		}
		else if (!d.retain_craft)
		{
			m.transport_released = true;
			s.mission.clear();
			craft_return(e);
		}
		else
			s.order = "hold";
		// Disabling a consumed craft moves its Flecs table; reacquire the component.
		++e.get_mut<CraftState>().revision;
	}
}

auto Runtime::craft_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai) -> bool
{
	if (!ai && !manual_workload(a, "craft"))
		return false;
	if (!native_command_envelope(c))
		return false;
	EntityId id = 0;
	for (const auto &[candidate, e] : craft_rows())
		if (c.system == std::to_string(candidate))
			id = candidate;
	if (!id)
		return false;
	auto e = entities.at(id);
	auto &s = e.get_mut<CraftState>();
	if (s.home != a.ship || s.revision != static_cast<std::uint64_t>(c.value) || s.revision >= 9007199254740990ULL ||
	    enabled_system_owner(a.ship, s.bay) != a || combat_dead(e))
		return false;
	bool authority = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    ctrl.generation == c.generation)
		{
			if (!ctrl.definition.widget.empty())
				authority |=
				    controller_operation_owns(ctrl, "craft", s.bay) && controller_operation_human(ctrl, "craft") != ai;
			else if (ctrl.human != ai || (!ai && assisted_workload(a, "craft")))
				for (const auto &cmd : ctrl.definition.commands)
					for (auto consumer : cmd.consumers)
						authority |= definition(a.ship).fitted->values.at(consumer).id == s.bay;
		}
	if (!authority)
		return false;
	const auto &d = *definition(id).craft;
	const auto action = c.action.substr(6);
	if (action == "recall")
	{
		if (!c.text.empty() || s.phase == CraftPhase::Lost || s.phase == CraftPhase::Consumed || torpedo_launched(e) ||
		    (s.phase == CraftPhase::Docked && s.mission.empty()))
			return false;
		if (execute)
		{
			s.pending_order = "return";
			s.pending_target = 0;
			++s.revision;
		}
		return true;
	}
	if (cargo_committed(id) || !s.mission.empty() || ftl_preparing(a.ship) || !bay_working(a.ship, s.bay) ||
	    !((s.phase == CraftPhase::Docked && craft_available(id)) || s.phase == CraftPhase::Flying))
		return false;
	if (!((action == "defend" && d.role == "interceptor") || (action == "escort" && d.role == "interceptor") ||
	      (action == "bomb" && d.role == "bomber")))
		return false;
	EntityId target = a.ship;
	if (action != "defend")
	{
		target = 0;
		for (const auto &contact : capture_sensor_contacts(a))
			if (c.text == std::to_string(contact.id) && entities.contains(contact.id) && contact.id != id &&
			    (action != "bomb" || contact.disposition == "hostile"))
				target = contact.id;
		if (action == "escort" && c.text == std::to_string(a.ship))
			target = a.ship;
		if (!target)
			return false;
	}
	else if (!c.text.empty())
		return false;
	if (execute)
	{
		s.pending_order = action;
		s.pending_target = target;
		s.pending_position = entities.at(target).get<Pose>().position;
		s.pending_velocity = entities.at(target).get<Motion>().velocity;
		s.pending_altitude = altitude(target);
		++s.revision;
	}
	return true;
}

auto Runtime::launch_craft(flecs::entity e) -> void
{
	auto &s = e.get_mut<CraftState>();
	s.phase = CraftPhase::Launching;
	s.progress = 0;
	++s.revision;
}

auto Runtime::prepare_craft() -> void
{
	// This phase only queues orders; no craft has launched or moved yet. All craft
	// using a watch can share its current permitted contacts until those orders apply.
	std::map<ConsoleAddress, std::vector<BodyView>> watch_contacts;
	// Assisted combat craft use ordinary admitted orders and the watch's evidence.
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		const auto owner = enabled_system_owner(s.home, s.bay);
		const auto &workloads = entities.at(s.home).get<EntityControls>().layout.manual_workloads;
		const auto *operation = native_operation_controller(owner, "craft", s.bay);
		const bool automated =
		    operation
		        ? !controller_operation_human(*operation, "craft")
		        : (workloads.contains(owner.console) && workloads.at(owner.console).contains("craft") &&
		           !(console_operator(owner) != 0 && station_stars(owner) >= workloads.at(owner.console).at("craft")));
		if (!automated || !s.mission.empty() || away_craft_reserved(id) || cargo_committed(id) ||
		    !s.pending_order.empty())
			continue;
		const auto &role = definition(id).craft->role;
		if (role != "interceptor" && role != "bomber")
			continue;
		EntityId target = 0;
		for (const auto &controller : entities.at(s.home).get<EntityControls>().controllers)
			if (controller.definition.console == owner.console)
				for (std::size_t j = 0; j < controller.commands.size(); ++j)
					if (controller.definition.commands[j].label_key == "Combat target")
						target = controller.commands[j].value;
		bool hostile = false, threat = false;
		if (!watch_contacts.contains(owner))
			watch_contacts.emplace(owner, capture_sensor_contacts(owner));
		for (const auto &contact : watch_contacts.at(owner))
		{
			threat |= contact.disposition == "hostile";
			hostile |= contact.id == target && contact.disposition == "hostile";
		}
		if (s.order == "return" || s.phase == CraftPhase::Recovering ||
		    (s.phase == CraftPhase::Docked && (s.report.ammunition <= 0 || s.report.energy <= 0)))
			continue;
		const auto order = role == "interceptor" && threat                          ? std::string{"defend"}
		                   : role == "bomber" && hostile && s.report.ammunition > 0 ? std::string{"bomb"}
		                                                                            : std::string{"recall"};
		if ((s.phase == CraftPhase::Docked && (!craft_available(id) || order == "recall")) ||
		    (s.order == order && (order != "bomb" || s.target == target)))
			continue;
		for (const auto &controller : entities.at(s.home).get<EntityControls>().controllers)
			if (controller.definition.console == owner.console)
			{
				OperatingCommand command{s.home, "craft." + order, static_cast<std::int64_t>(s.revision),
				                         std::to_string(id)};
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				if (order == "bomb")
					command.text = std::to_string(target);
				if (craft_command(owner, command, true, !controller_operation_human(controller, "craft")))
					break;
			}
	}
	// Released transports compete for actual free craft when departure preparation begins.
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		auto &m = away_operations.missions()[i];
		const auto &d = away_operations.definitions()[i];
		const auto ship = away_ship(d);
		if (!ship || !ftl_recall_started(ship) || !m.deployed() || !m.transport.starts_with("craft:") ||
		    !m.transport_released)
			continue;
		for (const auto &[id, e] : craft_rows())
		{
			auto &s = e.get_mut<CraftState>();
			if (s.home != ship || definition(id).craft->role != "shuttle" || !s.mission.empty() ||
			    s.phase != CraftPhase::Docked || !bay_working(ship, s.bay) ||
			    m.assignments.size() + m.accompanying.size() >
			        static_cast<std::size_t>(definition(id).transport.capacity))
				continue;
			m.transport = "craft:" + std::to_string(id);
			s.mission = d.id;
			s.pending_order = "return";
			m.transport_released = false;
			m.phase = AwayPhase::Inbound;
			++m.revision;
			++s.revision;
			break;
		}
	}
	std::set<std::pair<EntityId, std::string>> busy;
	for (const auto &[id, e] : craft_rows())
	{
		{
			auto &s = e.get_mut<CraftState>();
			if (s.phase == CraftPhase::Lost || s.phase == CraftPhase::Consumed)
				continue;
			if (combat_dead(e) || (craft_dormant(e) && (!entities.contains(s.home) ||
			                                            (combat_dead(entities.at(s.home)) && !departed(s.home)))))
			{
				if (auto *body = e.try_get_mut<Destructible>())
				{
					const auto sources = rule_sources(e, true);
					body->durability = 0;
					body->destroyed_tick = ecs.get<Clock>().tick;
					emit_native_destroy_rules(e, {id}, &sources);
					body->processed = true;
				}
				if (auto *structure = e.try_get_mut<CombatStructure>())
					for (auto &compartment : structure->compartments)
					{
						compartment.hp = 0;
						compartment.disabled = true;
					}
				s.phase = CraftPhase::Lost;
				++s.revision;
				set_craft_active(e, false);
				continue;
			}
			if (ftl_recall_started(s.home) && !s.ftl_recalled && !torpedo_launched(e))
			{
				s.ftl_recalled = true;
				s.pending_order = "return";
				++s.revision;
			}
			if (!ftl_preparing(s.home))
				s.ftl_recalled = false;
			if (!s.pending_order.empty() && craft_link(id))
			{
				if (s.pending_order == "return")
				{
					if (!s.mission.empty())
						request_craft_pickup(id);
					else
						craft_return(e);
				}
				else
				{
					s.order = s.pending_order;
					s.target = s.pending_target;
					s.target_position = s.pending_position;
					s.target_velocity = s.pending_velocity;
					s.target_altitude = s.pending_altitude;
					s.pending_order.clear();
					if (s.phase == CraftPhase::Docked)
						launch_craft(e);
				}
			}
			const auto &bay = definition(s.home).craft_bays.at(s.bay);
			if (s.phase == CraftPhase::Launching || s.phase == CraftPhase::Recovering)
			{
				if (bay_working(s.home, s.bay) && busy.emplace(s.home, s.bay).second)
				{
					const auto transition =
					    advance_craft_transfer(s, bay, s.phase != CraftPhase::Recovering || craft_docking_ready(id));
					if (transition == CraftTransferProgress::Launched || transition == CraftTransferProgress::Recovered)
					{
						const bool launch = transition == CraftTransferProgress::Launched;
						if (launch)
						{
							auto p = entities.at(s.home).get<Pose>();
							p.position = p.position + fixed::scale(fixed::direction(p.heading), bay.docking_range);
							e.set<Pose>(p).set<Motion>(entities.at(s.home).get<Motion>());
							e.get_mut<VerticalMotion>().altitude =
							    altitude(s.home) + definition(s.home).half_height + definition(id).half_height + 1000;
						}
						else
						{
							complete_craft_recovery(id);
						}
						set_craft_active(e, launch);
					}
				}
			}
		}
		// Enabling/disabling a craft can move it between Flecs tables. Never
		// retain the pre-transition component reference for flight guidance.
		auto &s = e.get_mut<CraftState>();
		const auto &bay = definition(s.home).craft_bays.at(s.bay);
		if (s.phase != CraftPhase::Flying && s.phase != CraftPhase::Recovering)
			continue;
		const auto &d = *definition(id).craft;
		const ConsoleAddress observer{id, definition(id).consoles.front().id};
		const auto observed_contacts = capture_sensor_contacts(observer);
		if (craft_link(id))
		{
			s.rendezvous = entities.at(s.home).get<Pose>().position;
			s.rendezvous_velocity = entities.at(s.home).get<Motion>().velocity;
			s.rendezvous_altitude = altitude(s.home);
		}
		for (const auto &contact : observed_contacts)
		{
			if (contact.id == s.home)
			{
				s.rendezvous = {contact.x, contact.y};
				s.rendezvous_velocity = {contact.vx, contact.vy};
				s.rendezvous_altitude = contact.altitude;
			}
			if (contact.id == s.target)
			{
				s.target_position = {contact.x, contact.y};
				s.target_velocity = {contact.vx, contact.vy};
				s.target_altitude = contact.altitude;
			}
		}
		CraftNavigationFacts facts;
		facts.pose = e.get<Pose>();
		facts.altitude = e.get<VerticalMotion>().altitude;
		const auto destination = craft_navigation_destination(s);
		if (destination)
		{
			facts.flight_band = craft_flight_band(id);
			facts.own_half_height = definition(id).half_height;
			if (s.order == "return")
				facts.home_half_height = definition(s.home).half_height;
			if (bay.legacy_transport && s.order != "return")
				facts.destination_half_height = definition(destination).half_height;
			for (auto child : e.get<EquipmentIndex>().values)
				if (const auto *flight = child.try_get<FlightRuntime>();
				    flight && child.get<Equipment>().kind == OperatingSystem::Engine)
				{
					facts.maximum_speed = std::max(facts.maximum_speed, flight->definition.maximum_speed);
					facts.deceleration = std::max(facts.deceleration, flight->definition.deceleration);
				}
			facts.recovery_ready = s.order == "return" && s.phase == CraftPhase::Flying && craft_docking_ready(id);
		}
		e.set<CraftGuidance>(prepare_craft_navigation(s, d, bay, facts, observed_contacts));
	}
}
} // namespace phoenix::world_detail
