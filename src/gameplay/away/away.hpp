// World adapter. All mutable mission state is authoritative and saved.
AwayRuntime away_operations;
#include "native_bridge.hpp"
EntityId away_ship(const AwayDefinition &d) const
{
	return away_operations.origin(d, slot_entities);
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
	if (personnel_postings.contains(ship) && personnel_postings.at(ship).contains("officer:" + officer))
		return personnel_postings.at(ship).at("officer:" + officer).status;
	if (mission_ledger.lost.contains(ship) && mission_ledger.lost.at(ship).contains("officer:" + officer))
		return "Lost";
	return away_operations.officer_away(ship, officer, slot_entities) ? "Away" : std::string{};
}
bool away_reserved(EntityId ship, const std::string &asset) const
{
	if (personnel_postings.contains(ship) && personnel_postings.at(ship).contains(asset) &&
	    personnel_postings.at(ship).at(asset).status == "prize_crew")
		return true;
	if (boarding_defender_reserved(ship, asset))
		return true;
	if (mission_ledger.lost.contains(ship) && mission_ledger.lost.at(ship).contains(asset))
		return true;
	return away_operations.asset_assigned(ship, asset, slot_entities);
}
bool away_transport_requested(EntityId ship, const std::string &system) const
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
bool away_craft_reserved(EntityId craft, const std::string &except = {}) const
{
	const auto &state = entities.at(craft).get<CraftState>();
	return away_operations.craft_reserved(state.home, craft, except, slot_entities);
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
	const auto &resolved = definition(ship);
	const auto &personnel = entities.at(ship).get<PersonnelState>();
	return away_navigator_qualified(resolved.personnel, personnel, resolved.layout ? &*resolved.layout : nullptr,
	                                resolved.fitted ? &*resolved.fitted : nullptr, owner.console, system);
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
bool away_asset_healthy(EntityId ship, const std::string &asset) const
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
bool away_slot_accepts(EntityId ship, const AwaySlot &s, const std::string &asset) const
{
	const TeamDefinition *definition_team = nullptr;
	bool duty = false;
	if (s.kind == "team" && asset.starts_with("team:"))
	{
		if (const auto team = away_equipment(ship, asset.substr(5)); team)
		{
			duty = away_team_duty(ship, asset.substr(5)) != nullptr;
			if (duty && s.trait.empty())
				definition_team = &team.get<CombatMechanism>().definition.get<TeamDefinition>();
		}
	}
	return away_crew_slot_accepts(s, asset, definition(ship).personnel, definition_team, duty);
}
std::size_t away_origin(EntityId ship, const std::string &transport) const
{
	NativeAwayReadBinding binding(*this);
	return away_operations.staging_location(ship, transport, binding.observation);
}
bool away_staged(EntityId ship, const AwayMission &m) const
{
	NativeAwayReadBinding binding(*this);
	return away_operations.staged(ship, m, binding.observation);
}
std::int64_t away_transfer_ticks(const AwayDefinition &d, const AwayMission &m) const
{
	NativeAwayReadBinding binding(*this);
	return away_operations.transfer_ticks(d, m, binding.observation);
}
void mark_away_deployed(const AwayDefinition &definition, AwayMission &m)
{
	NativeAwayReadBinding binding(*this);
	away_operations.deploy(definition, m, binding.observation);
}
void finish_away_recovery(EntityId ship, AwayMission &m)
{
	NativeAwayBinding binding(*this);
	away_operations.recover(ship, m, binding.execution);
}
std::string boarding_insertion_blocker(std::size_t index, const std::string &transport) const
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
std::string away_blocker(std::size_t index, bool power = true) const
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
void prepare_away_officers(const ConsoleAddress &address, std::size_t index)
{
	if (station_stars(address) >= 3 && console_operator(address) != 0)
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
	auto &m = away_operations.mission(index);
	const auto &d = away_operations.definitions().at(index);
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
	auto it = std::find_if(away_operations.definitions().begin(), away_operations.definitions().end(),
	                       [&](const auto &d)
	                       {
		                       return d.id == c.system && away_ship(d) == a.ship;
	                       });
	if (it == away_operations.definitions().end())
		return false;
	const auto index = it - away_operations.definitions().begin();
	const auto &d = *it;
	auto &m = away_operations.missions()[index];
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
		for (std::size_t i = 0; i < away_operations.definitions().size(); ++i)
			if (away_operations.definitions()[i].id == c.system &&
			    away_ship(away_operations.definitions()[i]) == a.ship)
			{
				const auto &m = away_operations.missions()[i];
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
	if (m.phase == AwayPhase::Hidden || m.revision >= 9007199254740991ULL ||
	    away_operations.sequence() >= 9007199254740991ULL || c.value != static_cast<std::int64_t>(m.revision))
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
			m.sequence = away_operations.next_sequence();
			m.transfer = 0;
			if (supply)
			{
				auto job = *supply;
				job.id = next_cargo_delivery++;
				m.supply_job = job.id;
				m.supply_quantity = job.amount;
				cargo_deliveries.push_back(job);
				sync_cargo_stock(cargo_deliveries.back());
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
			m.sequence = away_operations.next_sequence();
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
void prepare_away_tick_contributions(std::size_t i)
{
	auto &m = away_operations.missions()[i];
	const auto &d = away_operations.definitions()[i];
	const auto ship = away_ship(d);
	if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
		for (const auto &console : definition(ship).consoles)
		{
			const ConsoleAddress owner{ship, console.id};
			if (!m.authorised_departments.contains(console.id))
				for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
					if (controller.definition.console == console.id && !controller.human)
					{
						bool team_controller = false;
						for (const auto &command : controller.definition.commands)
							for (const auto consumer : command.consumers)
								team_controller |=
								    definition(ship).fitted->values.at(consumer).kind == OperatingSystem::Team;
						if (console_operator(owner) && !team_controller)
							continue;
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
							OperatingCommand command{ship, "away.assign", static_cast<std::int64_t>(m.revision), d.id};
							command.text = slot.id + "/" + asset;
							command.controller = controller.definition.id;
							command.generation = controller.generation;
							away_command(owner, command, true, true);
						}
				}
}
bool prepare_away_assisted_send(std::size_t i)
{
	auto &m = away_operations.missions()[i];
	const auto &d = away_operations.definitions()[i];
	const auto ship = away_ship(d);
	const auto owner = enabled_system_owner(ship, away_dispatch_system(ship, d.dispatch));
	if (station_stars(owner) == 1)
		for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
			if (controller.definition.console == owner.console)
			{
				OperatingCommand command{ship, "away.send", static_cast<std::int64_t>(m.revision), d.id};
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				if (away_command(owner, command, true, !controller.human, true))
					return true;
			}
	return false;
}

void stage_away(EntityId ship, AwayMission &m)
{
	NativeAwayBinding binding(*this);
	away_operations.stage(ship, m, binding.execution);
}
void advance_away()
{
	NativeAwayBinding binding(*this);
	away_operations.advance(ecs.get<Clock>().tick, binding.execution);
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
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
		const auto &m = away_operations.missions()[i];
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
		row["automatic_dispatch"] =
		    !d.supply && d.allow_ship_transport && station_stars({a.ship, row.at("owner").get<std::string>()}) == 1;
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
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		Json row = away_operations.mission_record(i);
		for (const auto &[team, care] : away_operations.missions()[i].care)
		{
			const auto equipment = away_equipment(away_ship(away_operations.definitions()[i]), team);
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
	away_operations.restore_order(rows);
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
		const auto &record = rows[i];
		if (record.at("schema") != 11)
			throw std::runtime_error("Saved away mission schema retired; start a new session (schema 11 required)");
		const auto &care_record = record.at("care");
		if (!care_record.is_object() || care_record.size() > 16)
			throw std::runtime_error("Invalid away care roster");
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
		if (record.contains("deployed"))
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
			care.continuation = state_tree::load(order->program, care_record.at(team).at("continuation"));
		}
		validate_boarding_state(d, m);
		m.continuation = state_tree::load(d.program, rows[i].at("continuation"));
		away_operations.observe_sequence(m.sequence);
		away_operations.missions()[i] = std::move(m);
	}
}
