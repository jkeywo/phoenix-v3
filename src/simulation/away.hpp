// World adapter. All mutable mission state is authoritative and saved.
std::vector<AwayDefinition> away_definitions;
std::vector<AwayMission> away_missions;
std::uint64_t away_sequence = 0;
EntityId away_ship(const AwayDefinition &d) const
{
	return d.origin ? d.origin : slot_entities.contains(d.slot) ? slot_entities.at(d.slot) : 0;
}
EntityId away_transport_entity(EntityId ship, const std::string &transport) const
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
std::string canonical_away_transport(EntityId ship, const std::string &transport, const std::string &mission) const
{
	if (const auto bay = definition(ship).craft_bays.find(transport);
	    bay != definition(ship).craft_bays.end() && bay->second.legacy_transport)
		for (const auto &[id, e] : craft_rows())
			if (e.get<CraftState>().home == ship && e.get<CraftState>().bay == transport &&
			    craft_available(id, mission))
				return "craft:" + std::to_string(id);
	return transport;
}
bool away_ship_transport_owned(const ConsoleAddress &a) const
{
	return away_dispatch(a);
}
ConversationId away_conversation(const AwayDefinition &d) const
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
flecs::entity away_equipment(EntityId ship, const std::string &key) const
{
	for (auto e : entities.at(ship).get<EquipmentIndex>().values)
		if (e.get<Equipment>().id == key)
			return e;
	return {};
}
const PersonnelDuty *away_team_duty(EntityId ship, const std::string &key) const
{
	for (const auto &d : definition(ship).personnel.duties)
		if (d.team == key)
			return &d;
	return nullptr;
}
std::string away_officer_location(EntityId ship, const std::string &officer) const
{
	if (mission_ledger.lost.contains(ship) && mission_ledger.lost.at(ship).contains("officer:" + officer))
		return "Lost";
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_ship(away_definitions[i]) == ship && away_missions[i].absent())
		{
			const auto &m = away_missions[i];
			if (std::find(m.accompanying.begin(), m.accompanying.end(), officer) != m.accompanying.end())
				return "Away";
			for (const auto &[slot, asset] : m.assignments)
				if (asset == "officer:" + officer)
					return "Away";
		}
	return {};
}
bool away_reserved(EntityId ship, const std::string &asset) const
{
	if (boarding_defender_reserved(ship, asset))
		return true;
	if (mission_ledger.lost.contains(ship) && mission_ledger.lost.at(ship).contains(asset))
		return true;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_ship(away_definitions[i]) == ship)
		{
			const auto &m = away_missions[i];
			for (const auto &[slot, a] : m.assignments)
				if (a == asset)
					return true;
			if (asset.starts_with("officer:") &&
			    std::find(m.accompanying.begin(), m.accompanying.end(), asset.substr(8)) != m.accompanying.end())
				return true;
		}
	return false;
}
bool away_transport_requested(EntityId ship, const std::string &system) const
{
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &m = away_missions[i];
		if (away_ship(away_definitions[i]) != ship || (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
			continue;
		const auto craft = away_transport_entity(ship, m.transport);
		if ((m.transport == system || (craft && craft != ship && entities.at(craft).get<CraftState>().bay == system)) &&
		    away_blocker(i, false).empty())
			return true;
	}
	return false;
}
bool away_craft_reserved(EntityId craft, const std::string &except = {}) const
{
	const auto &state = entities.at(craft).get<CraftState>();
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &d = away_definitions[i];
		const auto &m = away_missions[i];
		if (away_ship(d) != state.home || d.id == except)
			continue;
		if ((!m.transport_released && m.transport == "craft:" + std::to_string(craft) && m.phase != AwayPhase::Lost &&
		     !(m.phase == AwayPhase::Recovered && !m.outcome.empty())) ||
		    std::any_of(m.escorts.begin(), m.escorts.end(),
		                [&](const auto &pair)
		                {
			                return pair.second == craft;
		                }))
			return true;
	}
	return false;
}
void return_away_escorts(AwayMission &m)
{
	for (const auto &[slot, id] : m.escorts)
		if (entities.contains(id) && entities.at(id).get<CraftState>().phase != CraftPhase::Lost)
			entities.at(id).get_mut<CraftState>().pending_order = "return";
}
bool away_qualified(EntityId ship, const std::string &system) const
{
	const auto owner = enabled_system_owner(ship, system);
	const auto &pd = definition(ship).personnel;
	const auto &ps = entities.at(ship).get<PersonnelState>();
	for (const auto &d : pd.duties)
		if (d.console == owner.console && !d.required_trait.empty())
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (ps.officers[i].duty == d.id && ps.officers[i].present && officer_healthy(ps.officers[i]) &&
				    std::find(pd.officers[i].traits.begin(), pd.officers[i].traits.end(), d.required_trait) !=
				        pd.officers[i].traits.end())
					for (const auto &c : definition(ship).layout->controllers)
						if (c.console == d.console && c.id == d.controller)
							for (const auto &command : c.commands)
								for (auto consumer : command.consumers)
									if (definition(ship).fitted->values.at(consumer).id == system)
										return true;
	return false;
}
std::string away_dispatch_system(EntityId ship, const std::string &route = {}) const
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
bool away_dispatch(const ConsoleAddress &a, const std::string &route = {}) const
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
bool away_asset_owned(const ConsoleAddress &a, const std::string &asset) const
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
bool away_has_capability(EntityId ship, const AwayMission &m, const std::string &capability) const
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
				return true;
	return false;
}
bool away_asset_healthy(EntityId ship, const std::string &asset) const
{
	const auto &pd = definition(ship).personnel;
	const auto &ps = entities.at(ship).get<PersonnelState>();
	if (asset.starts_with("team:"))
	{
		const auto team = away_equipment(ship, asset.substr(5));
		if (!team || !team_available(team.get<CombatMechanism>()))
			return false;
	}
	if (asset.starts_with("team:"))
		for (const auto &o : ps.officers)
			if (o.escort == asset.substr(5))
				return false;
	for (std::size_t i = 0; i < pd.officers.size(); ++i)
	{
		if (asset == "officer:" + pd.officers[i].id && !officer_healthy(ps.officers[i]))
			return false;
		if (asset.starts_with("team:"))
			if (const auto *duty = away_team_duty(ship, asset.substr(5));
			    duty && ps.officers[i].duty == duty->id && !officer_healthy(ps.officers[i]))
				return false;
	}
	return true;
}
bool away_slot_accepts(EntityId ship, const AwaySlot &s, const std::string &asset) const
{
	if (!asset.starts_with(s.kind + ":"))
		return false;
	if (s.kind == "team")
	{
		const auto team = away_equipment(ship, asset.substr(5));
		if (!team || !away_team_duty(ship, asset.substr(5)) || !s.trait.empty())
			return false;
		const auto &d = team.get<CombatMechanism>().definition.get<TeamDefinition>();
		return (s.team_type.empty() || s.team_type == d.type) &&
		       (s.capability.empty() || d.capabilities.contains(s.capability));
	}
	for (const auto &o : definition(ship).personnel.officers)
		if (o.id == asset.substr(8))
			return s.trait.empty() || std::find(o.traits.begin(), o.traits.end(), s.trait) != o.traits.end();
	return false;
}
std::size_t away_origin(EntityId ship, const std::string &transport) const
{
	const auto &d = definition(ship);
	const auto physical = away_transport_entity(ship, transport);
	if (physical == ship)
	{
		if (const auto *dock = entities.at(ship).try_get<DockConnection>())
			return d.combat->compartment_ids.at(d.docking.at(dock->port).compartment);
		if (!d.docking.empty())
			return d.combat->compartment_ids.at(d.docking.front().compartment);
	}
	const auto &name = physical == ship ? d.transport.compartment
	                   : physical       ? d.craft_bays.at(entities.at(physical).get<CraftState>().bay).compartment
	                                    : d.displacements.at(transport).compartment;
	return std::find(d.personnel.compartments.begin(), d.personnel.compartments.end(), name) -
	       d.personnel.compartments.begin();
}
bool away_staged(EntityId ship, const AwayMission &m) const
{
	if (m.transport.empty())
		return false;
	const auto location = away_origin(ship, m.transport);
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
		{
			const auto &r = away_equipment(ship, asset.substr(5)).get<CombatMechanism>().get<TeamState>();
			if (r.location != static_cast<std::int64_t>(location) || r.travel)
				return false;
		}
		else
		{
			const auto &pd = definition(ship).personnel;
			const auto &ps = entities.at(ship).get<PersonnelState>();
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (pd.officers[i].id == asset.substr(8) &&
				    (!officer_healthy(ps.officers[i]) || ps.officers[i].location != location ||
				     ps.officers[i].remaining))
					return false;
		}
	for (const auto &key : m.accompanying)
	{
		const auto &pd = definition(ship).personnel;
		const auto &ps = entities.at(ship).get<PersonnelState>();
		for (std::size_t i = 0; i < pd.officers.size(); ++i)
			if (pd.officers[i].id == key &&
			    (!officer_healthy(ps.officers[i]) || !ps.officers[i].present || ps.officers[i].remaining))
				return false;
	}
	return true;
}
std::int64_t away_transfer_ticks(const AwayDefinition &d, const AwayMission &m) const
{
	const auto ship = away_ship(d);
	const auto craft = away_transport_entity(ship, m.transport);
	if (!craft)
		return definition(ship).displacements.at(m.transport).ticks;
	if (craft != ship)
	{
		const auto &bay = definition(ship).craft_bays.at(entities.at(craft).get<CraftState>().bay);
		if (bay.legacy_transport)
			return bay.transfer_ticks;
	}
	return d.deployment_ticks;
}
void mark_away_deployed(const AwayDefinition &definition, AwayMission &m)
{
	m.care.clear();
	m.entry_compartment = 0;
	if (m.transport == "ship")
	{
		const auto ship = away_ship(definition);
		if (const auto *dock = entities.at(ship).try_get<DockConnection>())
			m.entry_compartment =
			    this->definition(definition.target)
			        .combat->compartment_ids.at(
			            this->definition(definition.target).docking.at(dock->remote_port).compartment);
	}
	if (definition.boarding)
	{
		m.boarding.location = definition.boarding->compartments.at(definition.boarding->entry);
		m.boarding.travel = 0;
		m.boarding.edge_to = -1;
	}
	m.phase = AwayPhase::Deployed;
	m.recovered = false;
	m.ever_deployed = true;
	m.transfer = 0;
	for (const auto &[slot, asset] : m.assignments)
		m.participants.insert(asset);
	for (const auto &officer : m.accompanying)
		m.participants.insert("officer:" + officer);
	++m.revision;
}
void finish_away_recovery(EntityId ship, AwayMission &m)
{
	const auto location = away_origin(ship, m.transport);
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
		{
			auto &r = away_equipment(ship, asset.substr(5)).get_mut<CombatMechanism>().get<TeamState>();
			r.location = location;
			r.destination = r.edge_to = -1;
			r.travel = r.edge_total = 0;
			r.order.clear();
			r.activity.clear();
			r.patient.clear();
			r.continuation = {};
			r.repair_auto = station_stars(enabled_system_owner(ship, asset.substr(5))) < 3;
			r.repair_system = -1;
		}
	auto &p = entities.at(ship).get_mut<PersonnelState>();
	const auto &d = definition(ship).personnel;
	for (std::size_t o = 0; o < d.officers.size(); ++o)
		if (std::find(m.accompanying.begin(), m.accompanying.end(), d.officers[o].id) != m.accompanying.end() ||
		    std::any_of(m.assignments.begin(), m.assignments.end(),
		                [&](const auto &a)
		                {
			                return a.second == "officer:" + d.officers[o].id;
		                }))
		{
			p.officers[o].location = location;
			p.officers[o].remaining = 0;
			p.officers[o].edge_to = -1;
		}
	++p.revision;
	m.aboard_craft = false;
	m.phase = AwayPhase::Recovered;
	m.recovered = true;
	m.transport_released = false;
	m.transfer = 0;
	m.assignments.clear();
	m.accompanying.clear();
	m.care.clear();
	++m.revision;
}
std::string boarding_insertion_blocker(std::size_t index, const std::string &transport) const
{
	const auto &d = away_definitions[index];
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
std::string away_blocker(std::size_t index, bool power = true) const
{
	const auto &d = away_definitions[index];
	const auto &m = away_missions[index];
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
void prepare_away_officers(const ConsoleAddress &address, std::size_t index)
{
	if (station_stars(address) >= 3 && console_operator(address) != 0)
		return;
	const auto &d = away_definitions.at(index);
	for (const auto &slot : d.slots)
		if (slot.kind == "officer" && !away_missions.at(index).assignments.contains(slot.id))
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
						                         static_cast<std::int64_t>(away_missions.at(index).revision), d.id};
						command.text = slot.id + "/" + asset;
						command.controller = controller.definition.id;
						command.generation = controller.generation;
						if (away_command(address, command, true, !controller.human, true))
						{
							assigned = true;
							break;
						}
					}
				if (assigned)
					break;
			}
}
void prepare_away_contribution(const ConsoleAddress &a, std::size_t index)
{
	auto &m = away_missions.at(index);
	const auto &d = away_definitions.at(index);
	if (!m.authorised_departments.contains(a.console) ||
	    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return;
	if ((station_stars(a) < 2 || console_operator(a) == 0) && m.transport.empty())
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
						away_command(a, c, true, !ctrl.human, true);
					}
	if (station_stars(a) < 3 || console_operator(a) == 0)
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
								away_command(a, c, true, !ctrl.human, true);
							}
	if (station_stars(a) < 2 || console_operator(a) == 0)
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
								away_command(a, c, true, !ctrl.human, true);
							}
	prepare_away_officers(a, index);
}
bool away_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false,
                  bool assisted = false)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
		return false;
	if (!entities.at(a.ship).has<EntityControls>())
		return false;
	auto it = std::find_if(away_definitions.begin(), away_definitions.end(),
	                       [&](const auto &d)
	                       {
		                       return d.id == c.system && away_ship(d) == a.ship;
	                       });
	if (it == away_definitions.end())
		return false;
	const auto index = it - away_definitions.begin();
	const auto &d = *it;
	auto &m = away_missions[index];
	std::string controlled_system;
	if (c.action == "away.transport" || c.action == "away.escort")
	{
		auto selection = c.action == "away.escort" ? c.text.substr(c.text.find('/') + 1)
		                 : c.text.empty()          ? m.transport
		                                           : c.text;
		if (selection.empty() && c.action == "away.escort")
		{
			const auto slot = c.text.substr(0, c.text.find('/'));
			if (m.escorts.contains(slot))
				selection = "craft:" + std::to_string(m.escorts.at(slot));
		}
		const auto physical = away_transport_entity(a.ship, selection);
		if (physical == a.ship)
		{
			for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
				if (e.get<Equipment>().kind == OperatingSystem::Dispatch &&
				    enabled_system_owner(a.ship, e.get<Equipment>().id) == a)
					controlled_system = e.get<Equipment>().id;
		}
		else
			controlled_system = physical ? entities.at(physical).get<CraftState>().bay : selection;
	}
	else if (c.action == "away.contribute")
	{
		if (c.text != "1" && c.text != "0")
			return false;
		for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
			if (ctrl.definition.id == c.controller && ctrl.definition.console == a.console)
				for (const auto &ch : ctrl.definition.commands)
					for (auto consumer : ch.consumers)
						if (const auto &system = definition(a.ship).fitted->values.at(consumer);
						    system.kind == OperatingSystem::Team || system.kind == OperatingSystem::ShuttleBay)
							controlled_system = system.id;
	}
	else if (c.action == "away.assign")
	{
		const auto pos = c.text.find("/team:");
		if (pos != std::string::npos)
			controlled_system = c.text.substr(pos + 6);
	}
	else if (c.action == "away.withdraw")
	{
		for (std::size_t i = 0; i < away_definitions.size(); ++i)
			if (away_definitions[i].id == c.system && away_ship(away_definitions[i]) == a.ship)
			{
				const auto &m = away_missions[i];
				if (m.assignments.contains(c.text) && m.assignments.at(c.text).starts_with("team:"))
					controlled_system = m.assignments.at(c.text).substr(5);
			}
	}
	else
	{
		if (!c.text.empty() && c.action != "away.quantity")
			return false;
		for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
			if (e.get<Equipment>().kind == OperatingSystem::Dispatch &&
			    enabled_system_owner(a.ship, e.get<Equipment>().id) == a &&
			    e.get<Equipment>().id == away_dispatch_system(a.ship, d.dispatch))
				controlled_system = e.get<Equipment>().id;
		if (controlled_system.empty())
			return false;
	}
	bool authority = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    (ctrl.human != ai || (!ai && c.action == "away.contribute" && !controlled_system.empty()) ||
		     (!ai && (c.action == "away.transport" || c.action == "away.escort") &&
		      assisted_workload(a, "transport")) ||
		     (!ai && station_stars(a) < 3 && !controlled_system.empty() &&
		      (c.action == "away.assign" || c.action == "away.withdraw"))) &&
		    ctrl.generation == c.generation)
		{
			if (controlled_system.empty())
				authority = true;
			for (const auto &command : ctrl.definition.commands)
				for (auto consumer : command.consumers)
					if (definition(a.ship).fitted->values.at(consumer).id == controlled_system)
						authority = true;
		}
	if (!authority)
		return false;
	if (m.phase == AwayPhase::Hidden || m.revision >= 9007199254740991ULL || away_sequence >= 9007199254740991ULL ||
	    c.value != static_cast<std::int64_t>(m.revision))
		return false;
	const auto action = c.action.substr(5);
	if (action == "quantity")
	{
		if (!d.supply || station_stars(a) < 3 || !away_dispatch(a, d.dispatch) ||
		    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || m.supply_job)
			return false;
		const auto quantity = parse_material_number(c.text);
		if (quantity < 0 || quantity > d.supply->limit)
			return false;
		const auto manifest = supply_manifest(index, quantity);
		if (quantity && (!manifest || manifest->amount != quantity))
			return false;
		if (execute)
		{
			m.supply_quantity = quantity;
			++m.revision;
		}
		return true;
	}
	if (action == "contribute")
	{
		if (controlled_system.empty() || (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered))
			return false;
		if (execute)
		{
			if (c.text == "1")
				m.authorised_departments.insert(a.console);
			else
			{
				m.authorised_departments.erase(a.console);
				if (!m.transport.empty() && away_asset_owned(a, "transport:" + m.transport))
					m.transport.clear();
				std::erase_if(m.escorts,
				              [&](const auto &s)
				              {
					              return away_asset_owned(a, "craft:" + std::to_string(s.second));
				              });
				std::vector<std::string> slots;
				for (const auto &[slot, asset] : m.assignments)
					if (away_asset_owned(a, asset))
						slots.push_back(slot);
				for (const auto &slot : slots)
					for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
						if (ctrl.definition.console == a.console && m.assignments.contains(slot))
						{
							OperatingCommand command{a.ship, "away.withdraw", static_cast<std::int64_t>(m.revision),
							                         d.id};
							command.controller = ctrl.definition.id;
							command.generation = ctrl.generation;
							command.text = slot;
							away_command(a, command, true, !ctrl.human, true);
						}
			}
			++m.revision;
			if (c.text == "1")
				prepare_away_contribution(a, index);
		}
		return true;
	}
	if (action == "open")
	{
		if (!away_dispatch(a, d.dispatch) || m.phase != AwayPhase::Available)
			return false;
		if (execute)
		{
			m.phase = AwayPhase::Open;
			m.authorised_departments.insert(a.console);
			if (station_stars(a) < 3 && d.allow_ship_transport)
				m.transport = "ship";
			++m.revision;
			prepare_away_officers(a, index);
			prepare_away_contribution(a, index);
		}
		return true;
	}
	if (action == "send")
	{
		const auto supply = d.supply ? supply_manifest(index) : std::optional<CargoDelivery>{};
		if (d.supply && (!supply || m.supply_job || cargo_deliveries.size() >= 128 || material_requests.size() >= 4094))
			return false;
		if (!away_dispatch(a, d.dispatch) || (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) ||
		    !m.outcome.empty() || !away_staged(a.ship, m))
			return false;
		if (!m.transport.empty() && !boarding_insertion_blocker(index, m.transport).empty())
			return false;
		if (away_transport_entity(a.ship, m.transport) == a.ship && !away_blocker(index, false).empty())
			return false;
		const auto craft = away_transport_entity(a.ship, m.transport);
		if (craft && craft != a.ship && (!craft_available(craft, d.id) || !away_blocker(index, false).empty()))
			return false;
		for (const auto &s : d.slots)
			if (s.required && !m.assignments.contains(s.id))
				return false;
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort" && slot.required && !m.escorts.contains(slot.id))
				return false;
		for (const auto &[slot, id] : m.escorts)
			if (!craft_available(id, d.id))
				return false;
		if (execute)
		{
			m.phase = AwayPhase::Outbound;
			m.sequence = ++away_sequence;
			m.transfer = 0;
			if (supply)
			{
				auto job = *supply;
				job.id = next_cargo_delivery++;
				m.supply_job = job.id;
				m.supply_quantity = job.amount;
				cargo_deliveries.push_back(job);
				std::erase_if(material_requests,
				              [&](const auto &r)
				              {
					              return r.automatic && r.destination == a.ship && r.to == job.from &&
					                     r.material == job.material;
				              });
				supply_transfer(cargo_deliveries.back(), a.ship, job.from, job.carrier, job.cargo, job.payment,
				                job.payment_amount);
			}
			if (craft && craft != a.ship)
			{
				auto e = entities.at(craft);
				auto &s = e.get_mut<CraftState>();
				s.mission = d.id;
				s.target = d.target;
				s.target_position = entities.at(d.target).get<Pose>().position;
				s.target_velocity = entities.at(d.target).get<Motion>().velocity;
				s.target_altitude = altitude(d.target);
				s.order = "transport";
				m.aboard_craft = true;
				m.transport_released = false;
				if (m.escorts.empty() && !d.supply)
					launch_craft(e);
			}
			for (const auto &[slot, id] : m.escorts)
			{
				auto e = entities.at(id);
				auto &escort = e.get_mut<CraftState>();
				escort.mission = d.id;
				escort.order = "escort";
				escort.target = craft;
				escort.target_position = entities.at(craft).get<Pose>().position;
				escort.target_velocity = entities.at(craft).get<Motion>().velocity;
				escort.target_altitude = altitude(craft);
				launch_craft(e);
			}
			++m.revision;
		}
		return true;
	}
	if (action == "recall")
	{
		if (d.supply && away_dispatch(a, d.dispatch) && m.supply_job &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound))
		{
			if (execute)
				for (auto &job : cargo_deliveries)
					if (job.id == m.supply_job)
					{
						request_cargo_return(job);
						++m.revision;
					}
			return true;
		}
		if (!away_dispatch(a, d.dispatch) || m.phase != AwayPhase::Deployed || m.boarding.prize_crew ||
		    !away_blocker(index, false).empty())
			return false;
		const auto craft = away_transport_entity(a.ship, m.transport);
		if (craft && craft != a.ship)
		{
			const auto &s = entities.at(craft).get<CraftState>();
			if ((!s.mission.empty() && s.mission != d.id) || s.phase == CraftPhase::Lost)
				return false;
		}
		if (execute)
		{
			m.phase = AwayPhase::Inbound;
			return_away_escorts(m);
			m.sequence = ++away_sequence;
			m.transfer = 0;
			++m.revision;
			if (craft && craft != a.ship)
			{
				// This is the party's recovery order. Independent craft orders retain radio delivery.
				request_craft_pickup(craft);
			}
		}
		return true;
	}
	if (action == "cancel")
	{
		if (away_dispatch(a, d.dispatch) && m.phase == AwayPhase::Open && !m.supply_job)
		{
			if (execute)
			{
				m.assignments.clear();
				m.accompanying.clear();
				m.care.clear();
				m.escorts.clear();
				m.transport.clear();
				m.authorised_departments.clear();
				m.phase = AwayPhase::Available;
				++m.revision;
			}
			return true;
		}
		if (!away_dispatch(a, d.dispatch) || (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
			return false;
		const auto selected = away_transport_entity(a.ship, m.transport);
		if (selected && selected != a.ship && torpedo_launched(entities.at(selected)))
			return false;
		if (execute)
		{
			return_away_escorts(m);
			if (d.supply)
			{
				for (auto &job : cargo_deliveries)
					if (job.id == m.supply_job)
						request_cargo_return(job);
				++m.revision;
				return true;
			}
			const auto craft = away_transport_entity(a.ship, m.transport);
			if (craft && craft != a.ship)
			{
				request_craft_pickup(craft);
				return true;
			}
			m.phase = m.deployed() ? AwayPhase::Deployed : AwayPhase::Open;
			m.transfer = 0;
			++m.revision;
		}
		return true;
	}
	if (action == "transport")
	{
		if (!ai && !assisted && !manual_workload(a, "transport"))
			return false;
		const bool deployed = m.phase == AwayPhase::Deployed;
		if (c.text.empty())
		{
			if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty() ||
			    m.transport.empty() || !away_asset_owned(a, "transport:" + m.transport))
				return false;
			if (execute)
			{
				m.transport.clear();
				++m.revision;
			}
			return true;
		}
		if ((!deployed && ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())) ||
		    m.aboard_craft || !away_asset_owned(a, "transport:" + c.text))
			return false;
		const auto selected = canonical_away_transport(a.ship, c.text, d.id);
		if (definition(a.ship).craft_bays.contains(c.text) && selected == c.text)
			return false;
		if (selected == "ship" && !d.allow_ship_transport)
			return false;
		const auto craft = away_transport_entity(a.ship, selected);
		for (const auto &slot : d.craft_slots)
			if (slot.role == "transport" &&
			    (!craft || craft == a.ship ||
			     (!slot.craft_role.empty() && definition(craft).craft->role != slot.craft_role)))
				return false;
		if (craft && craft != a.ship && !craft_available(craft, d.id) &&
		    !(deployed && selected == m.transport && entities.at(craft).get<CraftState>().mission == d.id))
			return false;
		if (execute)
		{
			if (deployed && selected != m.transport)
				release_away_transport(a.ship, m);
			m.transport = selected;
			if (deployed)
				m.transport_released = craft && craft != a.ship && entities.at(craft).get<CraftState>().mission != d.id;
			++m.revision;
		}
		return true;
	}
	if (action == "escort")
	{
		if ((!ai && !assisted && !manual_workload(a, "transport")) ||
		    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
			return false;
		const auto slash = c.text.find('/');
		if (slash == std::string::npos)
			return false;
		const auto slot = c.text.substr(0, slash), asset = c.text.substr(slash + 1);
		if (asset.empty())
		{
			if (!m.escorts.contains(slot) || !away_asset_owned(a, "craft:" + std::to_string(m.escorts.at(slot))))
				return false;
			if (execute)
			{
				m.escorts.erase(slot);
				++m.revision;
			}
			return true;
		}
		const auto id = away_transport_entity(a.ship, asset);
		if (!id || id == a.ship || !away_asset_owned(a, asset) || !craft_available(id, d.id) || m.transport == asset ||
		    std::any_of(m.escorts.begin(), m.escorts.end(),
		                [&](const auto &s)
		                {
			                return s.first != slot && s.second == id;
		                }))
			return false;
		const auto authored = std::find_if(d.craft_slots.begin(), d.craft_slots.end(),
		                                   [&](const auto &s)
		                                   {
			                                   return s.id == slot && s.role == "escort";
		                                   });
		if (authored == d.craft_slots.end() || definition(id).craft->role != authored->craft_role)
			return false;
		if (execute)
		{
			m.escorts[slot] = id;
			m.authorised_departments.insert(a.console);
			++m.revision;
			prepare_away_officers(a, index);
		}
		return true;
	}
	if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return false;
	if (action == "assign")
	{
		const auto separator = c.text.find('/');
		if (separator == std::string::npos)
			return false;
		const auto slot = c.text.substr(0, separator), asset = c.text.substr(separator + 1);
		if (!ai && !assisted && station_stars(a) < 3 && asset.starts_with("officer:"))
			return false;
		const auto s = std::find_if(d.slots.begin(), d.slots.end(),
		                            [&](const auto &s)
		                            {
			                            return s.id == slot;
		                            });
		if (s == d.slots.end() || m.assignments.contains(slot) || !away_asset_owned(a, asset) ||
		    away_reserved(a.ship, asset) || !away_asset_healthy(a.ship, asset) || !away_slot_accepts(a.ship, *s, asset))
			return false;
		std::vector<std::string> attached;
		const auto &pd = definition(a.ship).personnel;
		auto &ps = entities.at(a.ship).get_mut<PersonnelState>();
		if (asset.starts_with("team:"))
		{
			const auto *duty = away_team_duty(a.ship, asset.substr(5));
			if (execute && station_stars(a) < 3)
				auto_assign_officer(a, duty->id, true);
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (ps.officers[i].duty == duty->id)
				{
					if (away_reserved(a.ship, "officer:" + pd.officers[i].id))
						return false;
					attached.push_back(pd.officers[i].id);
				}
		}
		if (execute)
		{
			m.assignments.emplace(slot, asset);
			m.accompanying.insert(m.accompanying.end(), attached.begin(), attached.end());
			if (asset.starts_with("officer:"))
				for (std::size_t i = 0; i < pd.officers.size(); ++i)
					if (pd.officers[i].id == asset.substr(8))
					{
						ps.officers[i].duty.clear();
						ps.officers[i].present = false;
					}
			++ps.revision;
			++m.revision;
			if (!assisted)
				prepare_away_officers(a, index);
		}
		return true;
	}
	if (action == "withdraw")
	{
		if (!m.assignments.contains(c.text) || !away_asset_owned(a, m.assignments.at(c.text)))
			return false;
		if (!ai && !assisted && station_stars(a) < 3 && m.assignments.at(c.text).starts_with("officer:"))
			return false;
		if (execute)
		{
			const auto asset = m.assignments.at(c.text);
			if (asset.starts_with("team:"))
			{
				const auto *duty = away_team_duty(a.ship, asset.substr(5));
				const auto &pd = definition(a.ship).personnel;
				const auto &ps = entities.at(a.ship).get<PersonnelState>();
				for (std::size_t i = 0; i < pd.officers.size(); ++i)
					if (ps.officers[i].duty == duty->id)
						std::erase(m.accompanying, pd.officers[i].id);
			}
			m.assignments.erase(c.text);
			++m.revision;
		}
		return true;
	}
	return false;
}
void stage_away(EntityId ship, AwayMission &m)
{
	if (m.transport.empty() || m.absent())
		return;
	const auto target = away_origin(ship, m.transport);
	const auto &pd = definition(ship).personnel;
	for (const auto &[slot, asset] : m.assignments)
	{
		if (asset.starts_with("team:"))
		{
			const auto team = away_equipment(ship, asset.substr(5));
			auto &r = team.get_mut<CombatMechanism>().get<TeamState>();
			r.order.clear();
			r.activity.clear();
			r.patient.clear();
			r.continuation = {};
			r.repair_auto = false;
			r.repair_system = -1;
			r.destination = -1;
			if (advance_repair_team(entities.at(ship), team) && r.location != static_cast<std::int64_t>(target))
				route_repair_team(entities.at(ship), r, target);
		}
		else
		{
			auto &ps = entities.at(ship).get_mut<PersonnelState>();
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (pd.officers[i].id == asset.substr(8))
				{
					if (!officer_healthy(ps.officers[i]))
						continue;
					auto &o = ps.officers[i];
					if (o.remaining == 0 && o.location != target)
					{
						auto route = physical_route(ship, pd.compartments.size(), pd.links, o.location, target);
						o.edge_to = route.first;
						o.remaining = route.second;
					}
				}
		}
	}
}
void advance_away()
{
	const auto tick = ecs.get<Clock>().tick;
	std::map<std::pair<EntityId, std::string>, std::size_t> active;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &m = away_missions[i];
		if (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound)
		{
			auto key = std::pair{away_ship(away_definitions[i]), m.transport};
			if (!active.contains(key) || m.sequence < away_missions[active.at(key)].sequence)
				active[key] = i;
		}
	}
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		const auto &d = away_definitions[i];
		const auto ship = away_ship(d);
		if (!ship || (departed(ship) && !m.absent()) || m.phase == AwayPhase::Lost)
			continue;
		const auto cid = away_conversation(d);
		for (auto at = m.escorts.begin(); at != m.escorts.end();)
			if (entities.at(at->second).get<CraftState>().phase == CraftPhase::Lost)
			{
				m.flags.insert("escort_lost." + at->first);
				at = m.escorts.erase(at);
				++m.revision;
			}
			else
				++at;
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
			for (const auto &console : definition(ship).consoles)
			{
				const ConsoleAddress owner{ship, console.id};
				if (!console_operator(owner) && !m.authorised_departments.contains(console.id))
					for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
						if (controller.definition.console == console.id && !controller.human)
						{
							OperatingCommand c{ship, "away.contribute", static_cast<std::int64_t>(m.revision), d.id};
							c.controller = controller.definition.id;
							c.generation = controller.generation;
							c.text = "1";
							if (away_command(owner, c, true, true))
								break;
						}
				prepare_away_contribution(owner, i);
			}
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
			for (const auto &slot : d.slots)
				if (slot.assign_ai && slot.kind == "team" && !m.assignments.contains(slot.id) && m.outcome.empty())
					for (const auto &duty : definition(ship).personnel.duties)
					{
						if (duty.team.empty())
							continue;
						const auto asset = "team:" + duty.team;
						if (away_reserved(ship, asset) || !away_asset_healthy(ship, asset) ||
						    !away_slot_accepts(ship, slot, asset))
							continue;
						const auto owner = enabled_system_owner(ship, duty.team);
						for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
							if (controller.definition.console == owner.console && !controller.human &&
							    !m.assignments.contains(slot.id))
							{
								OperatingCommand command{ship, "away.assign", static_cast<std::int64_t>(m.revision),
								                         d.id};
								command.text = slot.id + "/" + asset;
								command.controller = controller.definition.id;
								command.generation = controller.generation;
								away_command(owner, command, true, true);
							}
					}
		if (m.phase == AwayPhase::Hidden)
		{
			if (d.boarding && boarding_visible(ship, d.target))
			{
				m.phase = AwayPhase::Available;
				++m.revision;
			}
			// Ordinary missions retain their delivered-introduction disclosure.
			if (cid && conversations.conversations.contains(cid))
			{
				const auto &c = conversations.conversations.at(cid);
				const auto self = "ship:" + std::to_string(ship);
				for (const auto &msg : c.messages)
					if (msg.text == d.reveal_event && msg.delivered.contains(self))
					{
						m.phase = AwayPhase::Available;
						++m.revision;
						break;
					}
			}
			if (m.phase == AwayPhase::Hidden)
				continue;
		}
		if (d.supply)
		{
			if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
				stage_away(ship, m);
			advance_supply_mission(i);
			continue;
		}
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Outbound)
			stage_away(ship, m);
		const auto transport = away_transport_entity(ship, m.transport);
		if (transport && transport != ship && m.phase == AwayPhase::Outbound &&
		    entities.at(transport).get<CraftState>().phase == CraftPhase::Docked &&
		    entities.at(transport).get<CraftState>().order == "transport" &&
		    std::all_of(m.escorts.begin(), m.escorts.end(),
		                [&](const auto &s)
		                {
			                return entities.at(s.second).template get<CraftState>().phase == CraftPhase::Flying;
		                }))
			launch_craft(entities.at(transport));
		if (transport && transport != ship && !m.transport_released &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.phase == AwayPhase::Deployed) &&
		    entities.at(transport).get<CraftState>().phase == CraftPhase::Lost)
		{
			m.outcome = "fail:Transport lost";
			m.phase = AwayPhase::Lost;
			return_away_escorts(m);
			++m.revision;
		}
		if (transport && transport != ship)
			advance_craft_transport(i, transport);
		else if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound) &&
		         active.at({ship, m.transport}) == i && away_blocker(i).empty() &&
		         (m.deployed() || away_staged(ship, m)))
		{
			if (++m.transfer >= away_transfer_ticks(d, m))
			{
				if (m.phase == AwayPhase::Inbound)
					finish_away_recovery(ship, m);
				else
					mark_away_deployed(d, m);
			}
		}
		if (m.phase == AwayPhase::Deployed)
		{
			if (d.boarding)
				advance_boarding(i);
			else
				advance_away_medical(i);
		}
		if (m.outcome.empty())
		{
			const bool work_ready = m.work_capability.empty() || away_has_capability(ship, m, m.work_capability);
			if (m.deployed() && work_ready && m.work > 0 && m.timed_work)
				m.progress = std::min(m.work, m.progress + 1000);
			if (m.deployed() && work_ready && m.work > 0 && !m.timed_work)
			{
				std::vector<flecs::entity> teams;
				std::int64_t officer_work = 0;
				for (const auto &[slot, asset] : m.assignments)
					if (asset.starts_with("team:"))
					{
						const auto team = away_equipment(ship, asset.substr(5));
						if (team.get<CombatMechanism>().definition.get<TeamDefinition>().capabilities.contains(
						        "repair"))
							teams.push_back(team);
					}
					else
						for (const auto &officer : definition(ship).personnel.officers)
							if (asset == "officer:" + officer.id &&
							    officer_healthy(entities.at(ship).get<PersonnelState>().officers.at(
							        &officer - definition(ship).personnel.officers.data())))
							{
								officer_work += effective_property(
								    entities.at(ship), "away:" + d.id + ":officer:" + officer.id, "repair", 1000);
							}
				const auto effort = structural_repair_work(entities.at(ship), teams);
				// Standalone officers can perform generic work, but structural repair needs a team.
				const auto progress = teams.empty() ? officer_work : effort / rate;
				m.progress = std::min(m.work, m.progress + progress);
				if (!m.repair_compartment.empty() && !combat_dead(entities.at(d.target)))
					apply_structural_repair(entities.at(d.target).get_mut<CombatStructure>(),
					                        d.repair_compartments.at(m.repair_compartment), effort);
			}
			const bool repaired =
			    m.repair_compartment.empty() ||
			    entities.at(d.target)
			            .get<CombatStructure>()
			            .compartments.at(d.repair_compartments.at(m.repair_compartment))
			            .hp == entities.at(d.target)
			                       .get<CombatStructure>()
			                       .definition.compartments.at(d.repair_compartments.at(m.repair_compartment))
			                       .maximum;
			if (scenario && !scenario->runtime().definition().event_bindings.empty() && m.work > 0 &&
			    m.progress >= m.work && repaired && work_ready)
				mission_change("work/" + std::to_string(ship) + "/" + d.id + "/" + m.work_key, "completed",
				               "away.work_completed", ship, d.target, d.id, m.work_key);
			const auto answer = cid ? conversations.conversations.at(cid).prompt.answer : std::string{};
			using state_tree::Value;
			std::vector<Value> obs{Value::boolean(m.deployed()),
			                       Value::boolean(m.recovered),
			                       Value::boolean(m.work > 0 && m.progress >= m.work && repaired && work_ready),
			                       Value::integer(tick),
			                       Value::integer(m.progress),
			                       Value::string(answer),
			                       Value::string(m.checkpoint)};
			const bool present = !m.transport_released && transport && !combat_dead(entities.at(transport)) &&
			                     (transport == ship ? away_dock_ready(ship, d)
			                                        : distance_3d(transport, d.target) <= d.deployment_range) &&
			                     (transport == ship || !craft_dormant(entities.at(transport)));
			obs.push_back(Value::boolean(present));
			obs.push_back(Value::boolean(transport && !combat_dead(entities.at(transport)) &&
			                             (transport == ship || craft_available(transport, d.id))));
			for (std::size_t field = obs.size(); field < d.program.registry.observations.size() - (d.boarding ? 4 : 0);
			     ++field)
			{
				const auto &name = d.program.registry.observations[field].name;
				if (name.starts_with("flags."))
				{
					obs.push_back(Value::boolean(m.flags.contains(name.substr(6))));
					continue;
				}
				if (name.starts_with("events."))
				{
					obs.push_back(Value::boolean(scenario && scenario->runtime().has_event(name.substr(7), {})));
					continue;
				}
				if (name.starts_with("team_capabilities."))
				{
					obs.push_back(Value::boolean(away_has_capability(ship, m, name.substr(18))));
					continue;
				}
				const bool operational = name.starts_with("transport.operational.");
				const auto tag = name.substr(operational ? 22 : 21);
				bool has = false;
				if (transport)
				{
					const auto &t = definition(transport).transport;
					has = std::find(t.capabilities.begin(), t.capabilities.end(), tag) != t.capabilities.end();
					if (operational)
					{
						has &= present;
						if (t.capability_systems.contains(tag))
							for (const auto &system : t.capability_systems.at(tag))
								has &= away_equipment(transport, system).get<Equipment>().enabled &&
								       !structurally_disabled(entities.at(transport), system) &&
								       fault_effectiveness(entities.at(transport), system) > 0;
					}
				}
				obs.push_back(Value::boolean(has));
			}
			if (d.boarding)
			{
				obs.push_back(Value::boolean(!m.boarding.travel && m.boarding.destination == m.boarding.location));
				obs.push_back(Value::boolean(m.boarding.encounter_done));
				obs.push_back(Value::boolean(m.boarding.encounter_success));
				obs.push_back(Value::integer(m.boarding.defenders));
			}
			const auto outputs = state_tree::step(d.program, m.continuation, obs);
			for (const auto &o : outputs.outputs)
			{
				if (!m.outcome.empty())
					break;
				if (!admit_effect(m.effects, o))
					continue;
				const auto &name = d.program.registry.actions.at(o.action).name;
				const auto &arg = o.arguments.at(0);
				if (d.boarding && boarding_action(i, name, arg.text))
					continue;
				if (name == "work" || name == "timed_work")
				{
					m.timed_work = name == "timed_work";
					if (arg.number < 1 || arg.number > 1728000)
						throw std::runtime_error("Invalid away work duration");
					if (m.work_key != m.checkpoint || m.work != arg.number * 1000)
					{
						m.work_key = m.checkpoint;
						m.work = arg.number * 1000;
						m.progress = std::min(m.work, m.saved_progress[m.checkpoint]);
					}
				}
				else if (name == "restore_compartment")
				{
					if (!m.deployed())
						throw std::runtime_error("Away repairs require a deployed party");
					m.repair_compartment = arg.text;
				}
				else if (name == "checkpoint")
				{
					m.work_capability.clear();
					if (m.checkpoint != arg.text)
					{
						if (m.work > 0 && !m.work_key.empty())
							m.saved_progress[m.work_key] = m.progress;
						m.work = 0;
						m.progress = 0;
						m.repair_compartment.clear();
					}
					m.checkpoint = arg.text;
				}
				else if (name == "pause")
				{
					if (m.work > 0 && !m.work_key.empty())
						m.saved_progress[m.work_key] = m.progress;
					m.work = 0;
					m.progress = 0;
				}
				else if (name == "mark")
					m.flags.insert(arg.text);
				else if (name == "require_team")
					m.work_capability = arg.text;
				else if (name == "crew_deaths")
				{
					if (arg.number < 0 || arg.number > 4096)
						throw std::runtime_error("Invalid away casualty count");
					m.crew_deaths = arg.number;
				}
				else if (name == "offer" && cid)
					conversations.offer(cid, arg.text);
				else if (name == "say" && cid)
					conversations.report(cid, arg.text);
				else if (name == "log")
					mission_transition("mission.log", ship, d.target, d.id, arg.text);
				else if ((name == "injure_duty" || name == "kill_duty") && m.deployed())
				{
					if (m.assignments.contains(arg.text) && m.assignments.at(arg.text).starts_with("team:"))
						if (const auto *duty = away_team_duty(ship, m.assignments.at(arg.text).substr(5)))
						{
							auto &ps = entities.at(ship).get_mut<PersonnelState>();
							const auto &pd = definition(ship).personnel;
							for (std::size_t oi = 0; oi < pd.officers.size(); ++oi)
								if (ps.officers[oi].duty == duty->id && ps.officers[oi].condition != "dead" &&
								    std::find(m.accompanying.begin(), m.accompanying.end(), pd.officers[oi].id) !=
								        m.accompanying.end())
								{
									auto &officer = ps.officers[oi];
									officer.condition = name == "kill_duty" ? "dead" : "severe";
									officer.present = false;
									officer.casualty_tick = tick;
									officer.stun_until = officer.treatment = officer.recovery = officer.remaining = 0;
									officer.stabilised = false;
									officer.edge_to = -1;
									officer.facility.clear();
									officer.escort.clear();
									if (name == "kill_duty")
									{
										officer.duty.clear();
										std::erase(m.accompanying, pd.officers[oi].id);
									}
									++ps.revision;
									mission_transition("mission.casualty", ship, ship, pd.officers[oi].id,
									                   officer.condition);
									break;
								}
						}
				}
				else if (name == "release_transport" && m.phase == AwayPhase::Deployed && transport &&
				         transport != ship && !m.aboard_craft)
				{
					m.transport_released = true;
					auto e = entities.at(transport);
					e.get_mut<CraftState>().mission.clear();
					craft_return(e);
					++m.revision;
				}
				else if (name == "report" && cid)
					conversations.event(cid, arg.text);
				else if (name == "complete" || name == "fail")
				{
					m.outcome = name + ":" + arg.text;
					if (cid)
						conversations.conversations.at(cid).prompt = {};
				}
				else if (name == "emit" && scenario)
					scenario->runtime().emit({conversation_instance, ++conversation_event_sequence, 0}, arg.text,
					                         {{"ship", static_cast<std::int64_t>(ship)}});
			}
		}
	}
}
std::string away_asset_label(EntityId ship, const std::string &asset) const
{
	if (asset.starts_with("officer:"))
		for (const auto &o : definition(ship).personnel.officers)
			if (o.id == asset.substr(8))
				return o.name;
	if (asset.starts_with("team:"))
	{
		const auto *d = away_team_duty(ship, asset.substr(5));
		if (d)
			return d->label;
	}
	return asset;
}
Json away_board(const ConsoleAddress &a) const
{
	Json out = Json::array();
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &d = away_definitions[i];
		const auto &m = away_missions[i];
		if (away_ship(d) != a.ship || m.phase == AwayPhase::Hidden)
			continue;
		Json row{{"id", d.id},
		         {"title", d.title},
		         {"phase", m.phase},
		         {"result", m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Lost ? m.outcome : std::string{}},
		         {"revision", m.revision},
		         {"transport", m.transport},
		         {"target", d.target},
		         {"dispatch", away_dispatch(a, d.dispatch)},
		         {"blocker", away_blocker(i)},
		         {"ship_transport_reason", d.allow_ship_transport ? std::string{}
		                                   : d.ship_transport_reason.empty()
		                                       ? std::string{"Ship transport disabled by mission"}
		                                       : d.ship_transport_reason},
		         {"slots", Json::array()},
		         {"transports", Json::array()},
		         {"accompanying", m.accompanying}};
		row["owner"] = enabled_system_owner(a.ship, away_dispatch_system(a.ship, d.dispatch)).console;
		row["dispatch_system"] = away_dispatch_system(a.ship, d.dispatch);
		bool owns_assets = false;
		for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
			if (controller.definition.console == a.console)
				for (const auto &ch : controller.definition.commands)
					for (const auto consumer : ch.consumers)
						owns_assets |=
						    definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::Team ||
						    definition(a.ship).fitted->values.at(consumer).kind == OperatingSystem::ShuttleBay;
		row["can_contribute"] =
		    owns_assets && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered) && m.outcome.empty();
		row["contributing"] = m.authorised_departments.contains(a.console);
		row["transport_owned"] =
		    !m.transport.empty() && row["can_contribute"] == true && away_asset_owned(a, "transport:" + m.transport) &&
		    manual_workload(a, "transport") &&
		    (station_stars(a) >= 3 ||
		     entities.at(a.ship).get<EntityControls>().layout.manual_workloads.contains(a.console));
		row["craft_slots"] = Json::array();
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort")
			{
				const auto assigned =
				    m.escorts.contains(slot.id) ? "craft:" + std::to_string(m.escorts.at(slot.id)) : std::string{};
				Json r{{"id", slot.id},
				       {"role", slot.role},
				       {"required", slot.required},
				       {"assigned", assigned},
				       {"owned", !assigned.empty() && away_asset_owned(a, assigned) && manual_workload(a, "transport")},
				       {"lost", m.flags.contains("escort_lost." + slot.id)},
				       {"choices", Json::array()}};
				if (row["can_contribute"] == true && manual_workload(a, "transport"))
					for (const auto &[id, e] : craft_rows())
						if (e.get<CraftState>().home == a.ship && definition(id).craft->role == slot.craft_role &&
						    away_asset_owned(a, "craft:" + std::to_string(id)) && craft_available(id, d.id))
							r["choices"].push_back("craft:" + std::to_string(id));
				row["craft_slots"].push_back(r);
			}
		if (d.supply)
		{
			const auto manifest = !m.supply_job ? supply_manifest(i) : std::optional<CargoDelivery>{};
			const CargoDelivery *job = nullptr;
			for (const auto &candidate : cargo_deliveries)
				if (candidate.id == m.supply_job)
					job = &candidate;
			row["supply"] = {{"material", d.supply->material},
			                 {"payment", d.supply->payment},
			                 {"give", d.supply->give},
			                 {"pay", d.supply->pay},
			                 {"quantity", job        ? job->amount
			                              : manifest ? manifest->amount
			                                         : 0},
			                 {"payment_quantity", job        ? job->payment_amount
			                                      : manifest ? manifest->payment_amount
			                                                 : 0},
			                 {"phase", job ? job->report : "preparation"},
			                 {"delivered", job ? job->delivered : 0},
			                 {"manual", station_stars(a) >= 3 && away_dispatch(a, d.dispatch) && !m.supply_job},
			                 {"quantities", Json::array({0})}};
			if (manifest && row["supply"]["manual"] == true)
			{
				for (const auto q :
				     {std::max(d.supply->give, manifest->amount - d.supply->give), manifest->amount + d.supply->give})
					if (const auto chosen = supply_manifest(i, q); chosen && chosen->amount == q)
						row["supply"]["quantities"].push_back(q);
			}
			if (!m.supply_job && !manifest && !m.transport.empty())
				row["blocker"] = "No compatible supply manifest: check stock, payment, capacity and docking";
		}
		if (d.boarding && m.boarding.reported)
			row["boarding_check"] = {{"chance_permille", m.boarding.chance},
			                         {"defenders", m.boarding.defenders},
			                         {"encounter", m.boarding.encounter},
			                         {"factors", m.boarding.factors}};
		if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
		{
			if (!m.transport.empty() && !away_staged(a.ship, m))
				row["blocker"] = "Personnel moving to transport";
			for (const auto &slot : d.slots)
				if (slot.required && !m.assignments.contains(slot.id))
					row["blocker"] = "Mandatory assignments required";
		}
		for (const auto &[key, t] : definition(a.ship).displacements)
			if ((m.outcome.empty() || m.phase == AwayPhase::Deployed) && away_asset_owned(a, "transport:" + key))
				row["transports"].push_back(key);
		for (const auto &[key, bay] : definition(a.ship).craft_bays)
			if (bay.legacy_transport && away_asset_owned(a, "transport:" + key) &&
			    canonical_away_transport(a.ship, key, d.id) != key)
				row["transports"].push_back(key);
		if (m.outcome.empty() && d.allow_ship_transport && away_ship_transport_owned(a))
			row["transports"].push_back("ship");
		for (const auto &[craft, e] : craft_rows())
			if (e.get<CraftState>().home == a.ship &&
			    (definition(craft).craft->role == "shuttle" || definition(craft).craft->role == "boarding_torpedo") &&
			    away_asset_owned(a, "transport:craft:" + std::to_string(craft)) && craft_available(craft, d.id) &&
			    std::all_of(d.craft_slots.begin(), d.craft_slots.end(),
			                [&](const auto &slot)
			                {
				                return slot.role != "transport" || slot.craft_role.empty() ||
				                       slot.craft_role == definition(craft).craft->role;
			                }))
				row["transports"].push_back("craft:" + std::to_string(craft));
		for (const auto &s : d.slots)
		{
			const auto assigned = m.assignments.contains(s.id) ? m.assignments.at(s.id) : std::string{};
			Json r{{"id", s.id},
			       {"required", s.required},
			       {"assigned", assigned},
			       {"owned", !assigned.empty() && away_asset_owned(a, assigned) &&
			                     (station_stars(a) >= 3 || !assigned.starts_with("officer:"))},
			       {"choices", Json::array()}};

			if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
			{
				for (const auto &duty : definition(a.ship).personnel.duties)
					if (!duty.team.empty())
					{
						const auto asset = "team:" + duty.team;
						if (enabled_system_owner(a.ship, duty.team) == a && away_asset_owned(a, asset) &&
						    !away_reserved(a.ship, asset) && away_asset_healthy(a.ship, asset) &&
						    away_slot_accepts(a.ship, s, asset))
							r["choices"].push_back(asset);
					}
				if (station_stars(a) >= 3)
					for (const auto &o : definition(a.ship).personnel.officers)
					{
						const auto asset = "officer:" + o.id;
						if (away_asset_owned(a, asset) && !away_reserved(a.ship, asset) &&
						    away_slot_accepts(a.ship, s, asset))
							r["choices"].push_back(asset);
					}
			}
			row["slots"].push_back(r);
		}
		row["labels"] = Json::object();
		if (!manual_workload(a, "transport") ||
		    (station_stars(a) < 3 &&
		     !entities.at(a.ship).get<EntityControls>().layout.manual_workloads.contains(a.console)))
			row["transports"] = Json::array();
		row["labels"]["ship"] = "Own ship — docking";
		for (const auto &[craft, e] : craft_rows())
			if (e.get<CraftState>().home == a.ship)
				row["labels"]["craft:" + std::to_string(craft)] =
				    definition(craft).sensor_class + " #" + std::to_string(craft);
		for (const auto &slot : row["slots"])
		{
			const auto asset = slot.at("assigned").get<std::string>();
			if (!asset.empty())
				row["labels"][asset] = away_asset_label(a.ship, asset);
			for (const auto &choice : slot.at("choices"))
			{
				const auto key = choice.get<std::string>();
				row["labels"][key] = away_asset_label(a.ship, key);
			}
		}
		for (const auto &key : m.accompanying)
			row["labels"]["officer:" + key] = away_asset_label(a.ship, "officer:" + key);
		out.push_back(row);
	}
	return out;
}
Json save_away() const
{
	Json out = Json::array();
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		Json row = away_missions[i];
		row["schema"] = 11;
		row["id"] = away_definitions[i].id;
		row["continuation"] = state_tree::save(away_definitions[i].program, away_missions[i].continuation);
		for (const auto &[team, care] : away_missions[i].care)
		{
			const auto equipment = away_equipment(away_ship(away_definitions[i]), team);
			const auto *order = team_order_definition(equipment.get<CombatMechanism>(), care.order);
			if (!order)
				throw std::runtime_error("Missing away medical order");
			row["care"][team]["continuation"] = state_tree::save(order->program, care.continuation);
		}
		out.push_back(row);
	}
	return out;
}
void restore_away(const Json &rows)
{
	if (!rows.is_array() || rows.size() != away_definitions.size())
		throw std::runtime_error("Invalid saved away missions");
	// Dynamic target templates can have been discovered over several spawn ticks.
	// Restore their saved ordering by stable identity, rather than reconstruction order.
	std::vector<AwayDefinition> ordered;
	std::set<std::string> identities;
	for (const auto &row : rows)
	{
		const auto id = row.at("id").get<std::string>();
		const auto found = std::find_if(away_definitions.begin(), away_definitions.end(),
		                                [&](const auto &d)
		                                {
			                                return d.id == id;
		                                });
		if (!identities.insert(id).second || found == away_definitions.end())
			throw std::runtime_error("Away mission identity mismatch");
		ordered.push_back(*found);
	}
	away_definitions = std::move(ordered);
	away_sequence = 0;
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = away_definitions[i];
		const auto schema = rows[i].at("schema");
		if ((schema != 1 && schema != 2 && schema != 3 && schema != 4 && schema != 5 && schema != 6 && schema != 7 &&
		     schema != 8 && schema != 9 && schema != 10 && schema != 11) ||
		    rows[i].at("id") != d.id)
			throw std::runtime_error("Away mission identity mismatch");
		auto record = rows[i];
		if (schema == 1)
			record["repair_compartment"] = record.value("repair_compartment", std::string{});
		if (schema < 3)
		{
			record["ever_deployed"] =
			    record.value("recovered", false) || record.at("phase") == "deployed" || record.at("phase") == "inbound";
			record["participants"] = Json::array();
			if (record.at("ever_deployed") == true)
			{
				for (const auto &[key, a] : record.at("assignments").items())
					record["participants"].push_back(a);
				for (const auto &o : record.at("accompanying"))
					record["participants"].push_back("officer:" + o.get<std::string>());
			}
		}
		if (schema < 4 || (schema < 8 && !record.contains("care")))
			record["care"] = Json::object();
		auto care_record = record.at("care");
		if (!care_record.is_object() || care_record.size() > 16)
			throw std::runtime_error("Invalid away care roster");
		for (auto &[key, value] : record["care"].items())
		{
			value.erase("continuation");
			if (schema < 5)
			{
				value["activity"] = "";
				value["work_done"] = false;
				value["done"] = false;
			}
		}
		if (schema < 6)
			record["boarding"] = BoardingState{};
		if (schema < 7)
		{
			record["craft"] = 0;
			record["returning_without_delivery"] = false;
			record["boarding"]["assessment_message"] = 0;
		}
		if (schema < 8)
		{
			record["aboard_craft"] = record.value("aboard_craft", false);
			record["transport_released"] = record.value("transport_released", false);
		}
		if (schema < 10)
			record["entry_compartment"] = 0;
		auto m = record.get<AwayMission>();
		if (m.flags.size() > 64 || m.saved_progress.size() > 256 || m.crew_deaths < 0 || m.crew_deaths > 4096 ||
		    (!m.work_capability.empty() &&
		     std::none_of(d.program.registry.observations.begin(), d.program.registry.observations.end(),
		                  [&](const auto &o)
		                  {
			                  return o.name == "team_capabilities." + m.work_capability;
		                  })))
			throw std::runtime_error("Invalid away job continuation");
		for (const auto &[key, progress] : m.saved_progress)
			if (key.empty() || key.size() > 256 || progress < 0 || progress > 1728000000)
				throw std::runtime_error("Invalid saved away job progress");
		if (m.entry_compartment < 0 ||
		    (definition(d.target).combat &&
		     m.entry_compartment >= static_cast<std::int64_t>(definition(d.target).combat->compartments.size())))
			throw std::runtime_error("Invalid away entry compartment");
		if ((schema == 1 && rows[i].at("deployed").get<bool>() != m.deployed()) ||
		    (schema >= 2 && rows[i].contains("deployed")))
			throw std::runtime_error("Inconsistent saved deployment");
		if (!m.repair_compartment.empty() && !d.repair_compartments.contains(m.repair_compartment))
			throw std::runtime_error("Invalid saved repair target");
		const auto ship = away_ship(d);
		if (m.escorts.size() > 8 || (ship && m.authorised_departments.size() > definition(ship).consoles.size()) ||
		    m.supply_quantity < 0 || m.supply_quantity > 1000000000 || (m.supply_job && !d.supply))
			throw std::runtime_error("Invalid mission contributions");
		for (const auto &console : m.authorised_departments)
			if (!ship || !seat(ship, console))
				throw std::runtime_error("Invalid contributing department");
		std::set<EntityId> escorts;
		for (const auto &[slot, id] : m.escorts)
		{
			const auto fit = std::find_if(d.craft_slots.begin(), d.craft_slots.end(),
			                              [&](const auto &s)
			                              {
				                              return s.id == slot && s.role == "escort";
			                              });
			if (fit == d.craft_slots.end() || !entities.contains(id) || !entities.at(id).has<CraftState>() ||
			    entities.at(id).get<CraftState>().home != ship || definition(id).craft->role != fit->craft_role ||
			    !escorts.insert(id).second || m.transport == "craft:" + std::to_string(id))
				throw std::runtime_error("Invalid saved mission escort");
		}

		if (m.revision < 1 || m.revision > 9007199254740991ULL || m.sequence > 9007199254740991ULL || m.progress < 0 ||
		    m.work < 0 || m.progress > m.work || m.work > 1728000000 || m.transfer < 0 ||
		    m.effects.size() > effect_receipt_limit || m.assignments.size() > 16 || m.participants.size() > 256 ||
		    (!m.ever_deployed && !m.participants.empty()))
			throw std::runtime_error("Invalid away continuation");
		for (const auto &asset : m.participants)
		{
			bool valid = asset.starts_with("team:") && away_team_duty(ship, asset.substr(5));
			if (asset.starts_with("officer:"))
				for (const auto &o : definition(ship).personnel.officers)
					valid |= asset == "officer:" + o.id;
			if (!valid)
				throw std::runtime_error("Invalid away participant");
		}
		if (!m.transport.empty() && (!ship || (!definition(ship).displacements.contains(m.transport) &&
		                                       !away_transport_entity(ship, m.transport))))
			throw std::runtime_error("Invalid saved transport");
		if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.deployed()) && m.transport.empty())
			throw std::runtime_error("Missing saved transport");
		if ((m.phase == AwayPhase::Recovered && !m.recovered) || m.accompanying.size() > 128 ||
		    m.checkpoint.size() > 256 || m.work_key.size() > 256 || m.outcome.size() > 512)
			throw std::runtime_error("Inconsistent away phase");
		if (!m.transport.empty() && m.transfer >= away_transfer_ticks(d, m))
			throw std::runtime_error("Invalid transfer progress");
		std::set<std::string> assets;
		for (const auto &[key, asset] : m.assignments)
		{
			if (!assets.insert(asset).second)
				throw std::runtime_error("Duplicate away asset");
			auto s = std::find_if(d.slots.begin(), d.slots.end(),
			                      [&](const auto &s)
			                      {
				                      return s.id == key;
			                      });
			if (s == d.slots.end() || !away_slot_accepts(ship, *s, asset) || away_reserved(ship, asset))
				throw std::runtime_error("Invalid saved assignment");
		}
		std::set<std::string> attached;
		for (const auto &key : m.accompanying)
		{
			if (!attached.insert(key).second || assets.contains("officer:" + key) ||
			    away_reserved(ship, "officer:" + key))
				throw std::runtime_error("Duplicate accompanying officer");
			bool valid = false;
			const auto &pd = definition(ship).personnel;
			const auto &ps = entities.at(ship).get<PersonnelState>();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				if (pd.officers[j].id == key)
					for (const auto &[slot, asset] : m.assignments)
						if (asset.starts_with("team:") &&
						    ps.officers[j].duty == away_team_duty(ship, asset.substr(5))->id)
							valid = true;
			if (!valid)
				throw std::runtime_error("Invalid accompanying officer");
		}
		if (ship)
		{
			const auto &pd = definition(ship).personnel;
			const auto &ps = entities.at(ship).get<PersonnelState>();
			for (std::size_t j = 0; j < pd.officers.size(); ++j)
				for (const auto &[slot, asset] : m.assignments)
				{
					if (asset.starts_with("team:") &&
					    ps.officers[j].duty == away_team_duty(ship, asset.substr(5))->id &&
					    !attached.contains(pd.officers[j].id))
						throw std::runtime_error("Missing accompanying officer");
					if (asset == "officer:" + pd.officers[j].id && !ps.officers[j].duty.empty())
						throw std::runtime_error("Deployed officer still assigned aboard");
				}
		}
		for (auto &[team, care] : m.care)
		{
			bool assigned = false;
			for (const auto &[slot, asset] : m.assignments)
				if (asset == "team:" + team)
					assigned = true;
			const auto equipment = away_equipment(ship, team);
			if (!assigned || !equipment)
				throw std::runtime_error("Orphaned away care");
			const auto *order = team_order_definition(equipment.get<CombatMechanism>(), care.order);
			const auto &pd = definition(d.target).personnel;
			if (!order || order->target != "patients" || care.location < 0 ||
			    care.location >= static_cast<std::int64_t>(pd.compartments.size()) || care.travel < 0 ||
			    (care.travel > 0) != (care.edge_to >= 0))
				throw std::runtime_error("Invalid away care location/order");
			if (care.travel)
			{
				bool link = false;
				for (const auto &l : pd.links)
					if (((l.from == care.location && l.to == care.edge_to) ||
					     (l.to == care.location && l.from == care.edge_to)) &&
					    care.travel <= l.ticks)
						link = true;
				if (!link)
					throw std::runtime_error("Invalid away care route");
			}
			const auto &actions = order->program.registry.actions;
			if (!care.activity.empty() && std::none_of(actions.begin(), actions.end(),
			                                           [&](const auto &action)
			                                           {
				                                           return action.name == care.activity;
			                                           }))
				throw std::runtime_error("Invalid away clinical activity");
			// Validate legacy tree state too, then restart only its missing retained intent.
			care.continuation = state_tree::load(order->program, care_record.at(team).at("continuation"));
			if (schema < 5)
				care.continuation = {};
		}
		validate_boarding_state(d, m);
		m.continuation = state_tree::load(d.program, rows[i].at("continuation"));
		away_sequence = std::max(away_sequence, m.sequence);
		away_missions[i] = std::move(m);
	}
}
