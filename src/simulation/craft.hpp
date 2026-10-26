// World adapter for ordinary carried entities. Guidance is read by authored Controllers.
std::int64_t altitude(EntityId id) const
{
	const auto *v = entities.at(id).try_get<VerticalMotion>();
	return v ? v->altitude : entities.at(id).get<Pose>().altitude;
}
std::int64_t craft_flight_band(EntityId id) const
{
	const auto *state = entities.at(id).try_get<CraftState>();
	if (!state)
		return 0;
	const auto &bay = definition(state->home).craft_bays.at(state->bay);
	// Lower slot indices use higher bands for the bay's stable serial launch order.
	return bay.flight_spacing * (bay.slots - state->slot);
}
std::int64_t distance_3d(EntityId a, EntityId b) const
{
	const auto xy = entities.at(a).get<Pose>().position - entities.at(b).get<Pose>().position;
	const auto z = altitude(a) - altitude(b);
	return static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(xy, xy) + z * z)));
}
std::vector<std::pair<EntityId, flecs::entity>> craft_rows() const
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
bool craft_dormant(flecs::entity e) const
{
	const auto *c = e.try_get<CraftState>();
	return c && (c->phase == CraftPhase::Docked || c->phase == CraftPhase::Launching || c->phase == CraftPhase::Lost ||
	             c->phase == CraftPhase::Consumed);
}
bool bay_working(EntityId ship, const std::string &bay) const
{
	if (!entities.contains(ship) || departed(ship) || combat_dead(entities.at(ship)))
		return false;
	const auto equipment = away_equipment(ship, bay);
	return equipment && equipment.get<Equipment>().enabled && !structurally_disabled(entities.at(ship), bay) &&
	       fault_effectiveness(entities.at(ship), bay) > 0;
}
bool craft_docking_ready(EntityId id) const
{
	const auto e = entities.at(id);
	const auto &s = e.get<CraftState>();
	const auto &bay = definition(s.home).craft_bays.at(s.bay);
	return bay_working(s.home, s.bay) && distance_3d(id, s.home) <= bay.docking_range &&
	       (!bay.legacy_transport ||
	        fixed::length(e.get<Pose>().position - entities.at(s.home).get<Pose>().position) <= bay.arrival_distance) &&
	       fixed::length(e.get<Motion>().velocity - entities.at(s.home).get<Motion>().velocity) <= bay.docking_speed;
}
bool ftl_preparing(EntityId ship) const
{
	if (!entities.contains(ship))
		return false;
	for (auto e : entities.at(ship).get<EquipmentIndex>().values)
		if (e.has<FtlReserve>() && e.get<Equipment>().enabled)
			return true;
	return false;
}
bool ftl_recall_started(EntityId ship) const
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
bool craft_link(EntityId craft) const
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
void set_craft_active(flecs::entity e, bool active)
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
void initialise_craft(std::vector<EntityId> *created = nullptr)
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
void restore_spawned_craft(const Json &saved)
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
bool torpedo_launched(flecs::entity e) const
{
	const auto &s = e.get<CraftState>();
	return definition(e.get<StableId>().id).craft->role == "boarding_torpedo" && s.phase != CraftPhase::Docked &&
	       s.phase != CraftPhase::Launching;
}
void release_away_transport(EntityId ship, AwayMission &m)
{
	PropertyEligibilityMutation eligibility_change(personnel_property_revision, personnel_property_mutation_depth);
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
bool craft_available(EntityId id, const std::string &mission = {}) const
{
	const auto &s = entities.at(id).get<CraftState>();
	return !cargo_committed(id) && !combat_dead(entities.at(id)) && s.phase == CraftPhase::Docked &&
	       !away_craft_reserved(id, mission) && s.order == "idle" && s.pending_order.empty() &&
	       (s.mission.empty() || s.mission == mission) && bay_working(s.home, s.bay) && !ftl_preparing(s.home);
}
void craft_return(flecs::entity e)
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
void complete_craft_recovery(EntityId craft)
{
	PropertyEligibilityMutation eligibility_change(personnel_property_revision, personnel_property_mutation_depth);
	auto &s = entities.at(craft).get_mut<CraftState>();
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		if (away_definitions[i].id != s.mission || away_ship(away_definitions[i]) != s.home || !m.aboard_craft ||
		    m.transport != "craft:" + std::to_string(craft) || m.supply_job)
			continue;
		finish_away_recovery(s.home, m);
	}
	for (auto &mission : away_missions)
		if (std::erase_if(mission.escorts,
		                  [&](const auto &slot)
		                  {
			                  return slot.second == craft;
		                  }))
			++mission.revision;
	s.mission.clear();
}
void request_craft_pickup(EntityId craft)
{
	PropertyEligibilityMutation eligibility_change(personnel_property_revision, personnel_property_mutation_depth);
	auto e = entities.at(craft);
	auto &s = e.get_mut<CraftState>();
	if (torpedo_launched(e))
		return;
	for (auto &m : away_missions)
		for (auto at = m.escorts.begin(); at != m.escorts.end(); ++at)
			if (at->second == craft)
			{
				m.escorts.erase(at);
				++m.revision;
				s.mission.clear();
				craft_return(e);
				return;
			}
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		const auto &d = away_definitions[i];
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
void advance_craft_transport(std::size_t index, EntityId craft)
{
	PropertyEligibilityMutation eligibility_change(personnel_property_revision, personnel_property_mutation_depth);
	auto &m = away_missions[index];
	const auto &d = away_definitions[index];
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
bool craft_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	if (!ai && !manual_workload(a, "craft"))
		return false;
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
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
		    ctrl.generation == c.generation && (ctrl.human != ai || (!ai && assisted_workload(a, "craft"))))
			for (const auto &cmd : ctrl.definition.commands)
				for (auto consumer : cmd.consumers)
					authority |= definition(a.ship).fitted->values.at(consumer).id == s.bay;
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
void launch_craft(flecs::entity e)
{
	auto &s = e.get_mut<CraftState>();
	s.phase = CraftPhase::Launching;
	s.progress = 0;
	++s.revision;
}
void prepare_craft()
{
	PropertyEligibilityMutation eligibility_change(personnel_property_revision, personnel_property_mutation_depth);
	// This phase only queues orders; no craft has launched or moved yet. All craft
	// using a watch can share its current permitted contacts until those orders apply.
	std::map<ConsoleAddress, std::vector<BodyView>> watch_contacts;
	// Assisted combat craft use ordinary admitted orders and the watch's evidence.
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		const auto owner = enabled_system_owner(s.home, s.bay);
		const auto &workloads = entities.at(s.home).get<EntityControls>().layout.manual_workloads;
		if (!workloads.contains(owner.console) || !workloads.at(owner.console).contains("craft") ||
		    (console_operator(owner) != 0 && station_stars(owner) >= workloads.at(owner.console).at("craft")) ||
		    !s.mission.empty() || away_craft_reserved(id) || cargo_committed(id) || !s.pending_order.empty())
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
				if (craft_command(owner, command, true, !controller.human))
					break;
			}
	}
	// Released transports compete for actual free craft when departure preparation begins.
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		const auto &d = away_definitions[i];
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
					if (s.phase == CraftPhase::Recovering && !craft_docking_ready(id))
					{
						s.phase = CraftPhase::Flying;
						s.progress = 0;
					}
					else if (++s.progress >= bay.transfer_ticks)
					{
						const bool launch = s.phase == CraftPhase::Launching;
						s.phase = launch ? CraftPhase::Flying : CraftPhase::Docked;
						s.progress = 0;
						++s.revision;
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
							s.order = "idle";
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
		CraftGuidance g;
		g.destination = e.get<Pose>().position;
		g.altitude = e.get<VerticalMotion>().altitude;
		g.heading = e.get<Pose>().heading;
		EntityId destination = 0;
		if (s.order == "return")
		{
			destination = s.home;
		}
		else if (s.order == "transport" || s.order == "pickup" || s.order == "hold")
			destination = s.target;
		else if (s.order == "bomb" || s.order == "escort" || s.order == "defend")
			destination = s.target;
		g.target = s.target;
		if (destination)
		{
			g.navigate = true;
			g.destination = destination == s.home ? s.rendezvous : s.target_position;
			g.altitude = (destination == s.home ? s.rendezvous_altitude : s.target_altitude) +
			             (s.order == "return" ? 0 : d.cruise_altitude + craft_flight_band(id));
			fixed::Vec target_velocity = destination == s.home ? s.rendezvous_velocity : s.target_velocity;
			for (const auto &contact : observed_contacts)
				if (contact.id == destination)
				{
					g.destination = {contact.x, contact.y};
					g.altitude =
					    contact.altitude + (s.order == "return" ? 0 : d.cruise_altitude + craft_flight_band(id));
					target_velocity = {contact.vx, contact.vy};
				}
			if (bay.legacy_transport)
			{
				g.target = destination;
				if (s.order != "return")
					g.altitude =
					    s.target_altitude + definition(destination).half_height + definition(id).half_height + 1000;
			}
			if (s.order == "return")
			{
				// Brake at a recovery point above the carrier, rather than
				// pursuing its centre while descending through a large hull.
				const auto outward = e.get<Pose>().position - g.destination;
				const auto clearance = definition(s.home).half_height + definition(id).half_height + 1000;
				// Choose an interior recovery point in the actual 3D envelope.
				// A planar point near its edge relied on overshoot/coasting to enter
				// the envelope and can queue returning craft against one another.
				const auto vertical = std::min(clearance, bay.docking_range);
				const auto horizontal = static_cast<std::int64_t>(fixed::sqrt(
				    static_cast<std::uint64_t>(bay.docking_range * bay.docking_range - vertical * vertical)));
				const auto offset = bay.legacy_transport ? 0 : std::min(fixed::length(outward), horizontal / 2);
				g.destination = g.destination + fixed::scale(fixed::normal(outward), offset);
				const auto band_room = std::max<std::int64_t>(0, bay.docking_range / 2 - clearance);
				g.altitude += clearance + std::min(craft_flight_band(id), band_room);
			}
			const auto delta = g.destination - e.get<Pose>().position;
			const auto distance = fixed::length(delta);
			std::int64_t maximum = 0, acceleration = 1;
			for (auto child : e.get<EquipmentIndex>().values)
				if (const auto *f = child.try_get<FlightRuntime>();
				    f && child.get<Equipment>().kind == OperatingSystem::Engine)
				{
					maximum = std::max(maximum, f->definition.maximum_speed);
					acceleration = std::max(acceleration, f->definition.deceleration);
				}
			if (bay.legacy_transport)
				maximum = std::min(maximum, bay.cruise_speed);
			g.heading = distance > 100 ? combat_bearing(delta) : e.get<Pose>().heading;
			const auto separation = s.order == "bomb" ? std::min<std::int64_t>(d.engagement_range / 2, 30000)
			                                          : (s.order == "escort" || s.order == "defend" ? 10000 : 1000);
			g.speed = std::min(maximum, static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(
			                                2 * acceleration * std::max<std::int64_t>(0, distance - separation)))));
			// Request a destination-relative velocity; passive damping handles drift.
			const auto relative_speed = std::min(g.speed, std::max<std::int64_t>(0, distance - separation) / 2);
			auto desired_velocity = target_velocity + fixed::scale(fixed::normal(delta), relative_speed);
			const auto guidance = pursuit_guidance(e.get<Pose>(), desired_velocity, maximum, g.heading);
			g.heading = guidance.heading;
			g.speed = guidance.speed;
			if (distance < bay.docking_range && s.order == "return")
			{
				if (s.phase == CraftPhase::Flying && craft_docking_ready(id))
				{
					s.phase = CraftPhase::Recovering;
					s.progress = 0;
				}
			}
		}
		e.set<CraftGuidance>(g);
	}
}
void step_vertical()
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	vertical_query.each(
	    [&](flecs::entity e, const StableId &id, VerticalMotion &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	for (const auto &[id, e] : rows)
	{
		if (craft_dormant(e) || combat_dead(e))
			continue;
		auto &v = e.get_mut<VerticalMotion>();
		e.get_mut<Pose>().previous_altitude = v.altitude;
		const auto &d = *definition(id).craft;
		int effectiveness = 0;
		for (auto child : e.get<EquipmentIndex>().values)
			if (child.get<Equipment>().kind == OperatingSystem::Engine && child.get<Equipment>().enabled &&
			    (!child.has<FlightRuntime>() || child.get<FlightRuntime>().state.enabled) &&
			    !structurally_disabled(e, child.get<Equipment>().id))
			{
				const auto *p = e.try_get<ShipResources>();
				const auto paid = p && child.has<ResourceBinding>()
				                      ? p->grants.at(child.get<ResourceBinding>().slot).supply_permille
				                      : 1000;
				effectiveness = std::max(
				    effectiveness, static_cast<int>(paid * fault_effectiveness(e, child.get<Equipment>().id) / 1000));
			}
		const auto delta = v.desired - v.altitude;
		const auto stopping = static_cast<std::int64_t>(
		    fixed::sqrt(static_cast<std::uint64_t>(2 * d.vertical_acceleration * std::abs(delta))));
		const auto desired = std::min(d.vertical_speed, stopping) * (delta < 0 ? -1 : 1);
		v.acceleration_remainder += d.vertical_acceleration * effectiveness / 1000;
		const auto step = v.acceleration_remainder / rate;
		v.acceleration_remainder %= rate;
		v.velocity += std::clamp(desired - v.velocity, -step, step);
		v.remainder += v.velocity;
		v.altitude = std::clamp(v.altitude + v.remainder / rate, std::int64_t{-1000000}, std::int64_t{1000000});
		v.remainder %= rate;
		e.get_mut<Pose>().altitude = v.altitude;
		e.get_mut<Motion>().vertical_velocity = v.velocity;
	}
}
void service_craft()
{
	const auto rows = craft_rows();
	for (const auto &[id, e] : rows)
		if (craft_dormant(e) && e.get<CraftState>().phase != CraftPhase::Lost &&
		    e.get<CraftState>().phase != CraftPhase::Consumed)
			if (auto *p = e.try_get_mut<ShipResources>())
				p->network.dormant_tick();
	std::set<std::pair<EntityId, std::string>> visited;
	for (const auto &[id, e] : rows)
	{
		const auto &s = e.get<CraftState>();
		if (!visited.emplace(s.home, s.bay).second || !bay_working(s.home, s.bay))
			continue;
		const auto &d = definition(s.home).craft_bays.at(s.bay);
		auto &bs = away_equipment(s.home, s.bay).get_mut<CraftBayState>();
		bs.energy_remainder += d.charge_rate;
		auto energy = bs.energy_remainder / rate;
		bs.energy_remainder %= rate;
		std::vector<std::int64_t> rounds;
		for (std::size_t i = 0; i < d.ammunition.size(); ++i)
		{
			bs.ammunition_remainders[i] += d.ammunition[i].rate;
			rounds.push_back(bs.ammunition_remainders[i] / rate);
			bs.ammunition_remainders[i] %= rate;
		}
		for (const auto &[other, craft] : rows)
		{
			const auto &c = craft.get<CraftState>();
			if (c.home != s.home || c.bay != s.bay || c.phase != CraftPhase::Docked)
				continue;
			if (auto *source = entities.at(s.home).try_get_mut<ShipResources>();
			    source && energy && !d.power_store.empty())
				if (auto *target = craft.try_get_mut<ShipResources>())
					for (ResourceIndex store = 0; store < target->network.stores().size(); ++store)
						energy -= source->network.transfer_external(
						    target->network, source->definition.network.store_ids.at(d.power_store), store, energy);
			for (std::size_t i = 0; i < d.ammunition.size(); ++i)
			{
				const auto &connection = d.ammunition[i];
				const auto name = craft.get<Definition>().name;
				const auto bound = connection.destination_stores.find(name);
				if (bound == connection.destination_stores.end())
					continue;
				const auto to = bound->second;
				const auto &source_stock = material_stock(s.home, connection.source_store);
				const auto &resource = source_stock.selected();
				const auto &destination_stock = material_stock(other, to);
				if (resource.empty() || !source_stock.accepts(resource) || !destination_stock.accepts(resource))
					continue;
				const auto amount =
				    std::min(rounds[i], std::max<std::int64_t>(0, material_store(other, to).capacity /
				                                                          material_size(other, to, resource) -
				                                                      material_pending(other, to, resource)));
				if (amount > 0)
				{
					request_material(s.home, connection.source_store, other, to, resource, amount, false);
					rounds[i] -= amount;
				}
			}
		}
	}
}
void update_craft_reports()
{
	for (const auto &[id, e] : craft_rows())
	{
		if (!craft_link(id))
			continue;
		auto &s = e.get_mut<CraftState>();
		CraftReport r;
		r.phase = s.phase;
		r.order = s.order;
		r.target = s.target;
		r.tick = ecs.get<Clock>().tick;
		if (const auto *p = e.try_get<ShipResources>())
			for (std::size_t i = 0; i < p->network.stores().size(); ++i)
			{
				r.energy += p->network.stores()[i].energy();
				r.capacity += p->definition.network.stores[i].capacity;
			}
		if (const auto *c = e.try_get<CombatStructure>())
			for (std::size_t i = 0; i < c->compartments.size(); ++i)
			{
				r.condition += c->compartments[i].hp;
				r.maximum_condition += c->definition.compartments[i].maximum;
			}
		for (auto child : e.get<EquipmentIndex>().values)
			if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Magazine)
			{
				const auto &stock = physical_stock(e, m->get<MagazineState>().stock);
				r.ammunition += stock.quantity(stock.selected());
			}
		s.report = r;
	}
}
Json craft_board(const ConsoleAddress &a) const
{
	Json out = Json::array();
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		if (s.home != a.ship || enabled_system_owner(a.ship, s.bay) != a)
			continue;
		const auto &r = s.report;
		const auto blocker = !bay_working(s.home, s.bay) ? "Shuttle bay unavailable"
		                     : ftl_preparing(s.home)     ? "FTL preparation: launches suspended"
		                                                 : "";
		out.push_back({{"id", std::to_string(id)},
		               {"bay", s.bay},
		               {"role", definition(id).craft->role},
		               {"revision", s.revision},
		               {"phase", r.phase},
		               {"order", r.order},
		               {"target", r.target},
		               {"linked", craft_link(id)},
		               {"pending", !s.pending_order.empty()},
		               {"available", craft_available(id)},
		               {"report_tick", r.tick},
		               {"energy", r.energy},
		               {"capacity", r.capacity},
		               {"ammunition", r.ammunition},
		               {"condition", r.condition},
		               {"maximum_condition", r.maximum_condition}});
		out.back()["blocker"] = blocker;
		bool manual = false;
		for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
			if (controller.definition.console == a.console)
				for (const auto &command : controller.definition.commands)
					for (const auto consumer : command.consumers)
						if (definition(a.ship).fitted->values.at(consumer).id == s.bay)
							manual |= controller.human || assisted_workload(a, "craft");
		out.back()["manual"] = manual && manual_workload(a, "craft");
	}
	return out;
}
Json save_craft() const
{
	Json out{{"schema", 2}, {"entities", Json::array()}, {"bays", Json::array()}};
	std::set<std::pair<EntityId, std::string>> bays;
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		out["entities"].push_back({{"id", id}, {"state", s}});
		bays.emplace(s.home, s.bay);
	}
	for (const auto &[home, bay] : bays)
		out["bays"].push_back(
		    {{"home", home}, {"bay", bay}, {"state", away_equipment(home, bay).get<CraftBayState>()}});
	return out;
}
void restore_craft(const Json &saved)
{
	const auto initial = save_craft();
	if (saved.is_null() && initial.at("entities").empty())
		return;
	if (!saved.is_object() || (saved.at("schema") != 1 && saved.at("schema") != 2) ||
	    !saved.at("entities").is_array() || saved.at("entities").size() != initial.at("entities").size() ||
	    !saved.at("bays").is_array() || saved.at("bays").size() != initial.at("bays").size())
		throw std::runtime_error("Invalid saved craft roster");
	std::set<EntityId> seen;
	const std::set<std::string> phases{"docked", "launching", "flying", "recovering", "lost", "consumed"};
	const std::set<std::string> orders{"idle", "hold", "return", "defend", "escort", "bomb", "transport", "pickup"};
	for (const auto &row : saved.at("entities"))
	{
		const auto id = row.at("id").get<EntityId>();
		if (!entities.contains(id) || !entities.at(id).has<CraftState>() || !seen.insert(id).second ||
		    !phases.contains(row.at("state").at("phase").get<std::string>()) ||
		    !phases.contains(row.at("state").at("report").at("phase").get<std::string>()))
			throw std::runtime_error("Invalid saved craft identity/phase");
		auto e = entities.at(id);
		const auto s = row.at("state").get<CraftState>();
		const auto &expected = e.get<CraftState>();
		const auto &bay = definition(expected.home).craft_bays.at(expected.bay);
		const auto v = e.get<VerticalMotion>();
		for (const auto &point : {s.rendezvous, s.rendezvous_velocity, s.target_position, s.target_velocity,
		                          s.pending_position, s.pending_velocity})
			if (std::abs(point.x) > fixed::unit || std::abs(point.y) > fixed::unit)
				throw std::runtime_error("Invalid saved craft navigation sample");
		if (std::abs(s.rendezvous_altitude) > 1000000 || std::abs(s.target_altitude) > 1000000 ||
		    std::abs(s.pending_altitude) > 1000000 || (s.target && !entities.contains(s.target)) ||
		    (s.pending_target && !entities.contains(s.pending_target)))
			throw std::runtime_error("Invalid saved craft target");
		if (s.home != expected.home || s.bay != expected.bay || s.slot != expected.slot || !s.revision ||
		    s.revision > 9007199254740991ULL || s.progress < 0 || s.progress >= bay.transfer_ticks ||
		    !orders.contains(s.order) || (!s.pending_order.empty() && !orders.contains(s.pending_order)) ||
		    s.mission.size() > 64 || s.report.tick > ecs.get<Clock>().tick || s.report.energy < 0 ||
		    s.report.capacity < s.report.energy || s.report.ammunition < 0 || s.report.condition < 0 ||
		    s.report.maximum_condition < s.report.condition || std::abs(v.altitude) > 1000000 ||
		    std::abs(v.desired) > 1000000 || std::abs(v.velocity) > 1000000 || std::abs(v.remainder) >= rate ||
		    std::abs(v.acceleration_remainder) >= rate)
			throw std::runtime_error("Invalid saved craft continuation");
		if (saved.at("schema") == 1 && (s.phase == CraftPhase::Consumed || s.report.phase == CraftPhase::Consumed))
			throw std::runtime_error("Consumed craft requires continuation schema 2");
		if ((s.phase == CraftPhase::Consumed &&
		     (definition(id).craft->role != "boarding_torpedo" || !s.mission.empty() || s.order != "idle" ||
		      !s.pending_order.empty() || s.pending_target || s.progress)) ||
		    (definition(id).craft->role == "boarding_torpedo" &&
		     (s.phase == CraftPhase::Recovering || s.order == "return" || s.order == "pickup" ||
		      (!s.pending_order.empty() &&
		       (s.pending_order != "return" || (s.phase != CraftPhase::Docked && s.phase != CraftPhase::Launching) ||
		        s.pending_target)))))
			throw std::runtime_error("Invalid saved torpedo continuation");
		if (!s.mission.empty() && std::none_of(away_definitions.begin(), away_definitions.end(),
		                                       [&](const auto &d)
		                                       {
			                                       return d.id == s.mission && away_ship(d) == s.home;
		                                       }))
			throw std::runtime_error("Invalid saved craft commitment");
		e.set<CraftState>(s).set<VerticalMotion>(v);
		e.get_mut<Pose>().altitude = v.altitude;
		e.get_mut<Motion>().vertical_velocity = v.velocity;
		set_craft_active(e, !craft_dormant(e));
	}
	std::set<std::pair<EntityId, std::string>> bays;
	for (const auto &row : saved.at("bays"))
	{
		const auto home = row.at("home").get<EntityId>();
		const auto bay = row.at("bay").get<std::string>();
		if (!entities.contains(home) || !definition(home).craft_bays.contains(bay) || !bays.emplace(home, bay).second)
			throw std::runtime_error("Invalid saved bay identity");
		const auto state = row.at("state").get<CraftBayState>();
		if (state.energy_remainder < 0 || state.energy_remainder >= rate ||
		    state.ammunition_remainders.size() != definition(home).craft_bays.at(bay).ammunition.size() ||
		    std::any_of(
		        state.ammunition_remainders.begin(), state.ammunition_remainders.end(),
		        [](auto v)
		        {
			        return v < 0 || v >= rate;
		        }))
			throw std::runtime_error("Invalid saved servicing continuation");
		away_equipment(home, bay).set<CraftBayState>(state);
	}
}
void validate_craft_transports() const
{
	std::set<EntityId> reserved;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &m = away_missions[i];
		const auto &d = away_definitions[i];
		const auto home = away_ship(d);
		const auto id = home ? away_transport_entity(home, m.transport) : 0;
		if (m.aboard_craft &&
		    (!id || id == home ||
		     (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound && m.phase != AwayPhase::Lost)))
			throw std::runtime_error("Invalid saved passenger location");
		if (m.transport_released && (!id || id == home || m.aboard_craft || m.phase != AwayPhase::Deployed))
			throw std::runtime_error("Invalid saved shuttle release");
		if (id && id != home &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound ||
		     (m.phase == AwayPhase::Deployed && !m.transport_released)))
		{
			const auto &s = entities.at(id).get<CraftState>();
			if ((s.mission != d.id && !(m.supply_job && m.phase == AwayPhase::Inbound &&
			                            s.phase == CraftPhase::Docked && s.mission.empty())) ||
			    (s.phase == CraftPhase::Docked && !m.supply_job && m.escorts.empty() &&
			     (m.aboard_craft || s.pending_order != "return")) ||
			    s.phase == CraftPhase::Lost || s.phase == CraftPhase::Consumed)
				throw std::runtime_error("Saved shuttle commitment mismatch");
		}
		if (id && id != home && !m.transport_released && m.phase != AwayPhase::Lost &&
		    !(m.phase == AwayPhase::Recovered && !m.outcome.empty()) && !reserved.insert(id).second)
			throw std::runtime_error("Duplicate saved mission transport");
		for (const auto &[slot, escort] : m.escorts)
			if (!reserved.insert(escort).second)
				throw std::runtime_error("Duplicate saved mission escort");
	}
	for (const auto &[id, e] : craft_rows())
	{
		const auto &state = e.get<CraftState>();
		if (state.mission.empty())
			continue;
		bool committed = false;
		for (std::size_t i = 0; i < away_missions.size(); ++i)
		{
			const auto &mission = away_missions[i];
			committed |= away_definitions[i].id == state.mission && away_ship(away_definitions[i]) == state.home &&
			             (mission.transport == "craft:" + std::to_string(id) ||
			              std::any_of(mission.escorts.begin(), mission.escorts.end(),
			                          [&](const auto &s)
			                          {
				                          return s.second == id;
			                          })) &&
			             (!mission.transport_released || std::any_of(mission.escorts.begin(), mission.escorts.end(),
			                                                         [&](const auto &s)
			                                                         {
				                                                         return s.second == id;
			                                                         })) &&
			             (mission.phase == AwayPhase::Outbound || mission.phase == AwayPhase::Inbound ||
			              mission.phase == AwayPhase::Deployed || mission.phase == AwayPhase::Recovered ||
			              mission.phase == AwayPhase::Lost);
		}
		if (!committed)
			throw std::runtime_error("Orphaned saved craft commitment");
	}
}
