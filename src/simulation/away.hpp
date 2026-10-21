// World adapter. All mutable mission state is authoritative and saved.
std::vector<AwayDefinition> away_definitions;
std::vector<AwayMission> away_missions;
std::uint64_t away_sequence = 0;
EntityId away_ship(const AwayDefinition &d) const
{
	return slot_entities.contains(d.slot) ? slot_entities.at(d.slot) : 0;
}
ConversationId away_conversation(const AwayDefinition &d) const
{
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
		if (away_ship(away_definitions[i]) == ship && away_missions[i].deployed())
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
		if (away_ship(away_definitions[i]) == ship && away_missions[i].transport == system &&
		    (away_missions[i].phase == AwayPhase::Outbound || away_missions[i].phase == AwayPhase::Inbound) &&
		    away_blocker(i, false).empty())
			return true;
	return false;
}
bool away_qualified(EntityId ship, const std::string &system) const
{
	const auto owner = system_owner(ship, system);
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
bool away_dispatch(const ConsoleAddress &a) const
{
	if (combat_dead(entities.at(a.ship)))
		return false;
	for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
		if (e.get<Equipment>().kind == OperatingSystem::Dispatch && e.get<Equipment>().enabled &&
		    system_owner(a.ship, e.get<Equipment>().id) == a)
			return true;
	return false;
}
bool away_asset_owned(const ConsoleAddress &a, const std::string &asset) const
{
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
		return definition(a.ship).displacements.contains(asset.substr(10)) &&
		       system_owner(a.ship, asset.substr(10)) == a;
	return false;
}
bool away_asset_healthy(EntityId ship, const std::string &asset) const
{
	const auto &pd = definition(ship).personnel;
	const auto &ps = entities.at(ship).get<PersonnelState>();
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
	const auto &name = d.displacements.at(transport).compartment;
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
std::string away_blocker(std::size_t index, bool power = true) const
{
	const auto &d = away_definitions[index];
	const auto &m = away_missions[index];
	const auto ship = away_ship(d);
	if (!ship || !entities.contains(d.target))
		return "Destination unavailable";
	if (m.transport.empty())
		return "Transport required";
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
	for (auto id : {ship, d.target})
		for (auto e : entities.at(id).get<EquipmentIndex>().values)
			if (e.get<Equipment>().kind == OperatingSystem::Shield && e.get<Equipment>().enabled &&
			    e.get<CombatMechanism>().get<ShieldState>().warmup > 0 &&
			    e.get<CombatMechanism>().get<ShieldState>().strength > 0)
			{
				const auto &sector = e.get<CombatMechanism>().definition;
				const auto &pose = entities.at(id).get<Pose>();
				const auto other = entities.at(id == ship ? d.target : ship).get<Pose>().position;
				const auto bearing = combat_bearing(other - pose.position);
				const auto difference = (bearing - pose.heading - sector.bearing + 540000) % 360000 - 180000;
				if (std::abs(difference) <= sector.arc / 2)
					return "Lower shields for displacement";
			}
	if (power)
	{
		const auto *p = entities.at(ship).try_get<ShipResources>();
		if (!p || !equipment.has<ResourceBinding>() ||
		    p->grants.at(equipment.get<ResourceBinding>().slot).supply_permille != 1000)
			return "Insufficient displacement power";
	}
	return {};
}
bool away_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
		return false;
	if (!entities.at(a.ship).has<EntityControls>())
		return false;
	std::string controlled_system;
	if (c.action == "away.transport")
		controlled_system = c.text;
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
		if (!c.text.empty())
			return false;
		for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
			if (e.get<Equipment>().kind == OperatingSystem::Dispatch &&
			    system_owner(a.ship, e.get<Equipment>().id) == a)
				controlled_system = e.get<Equipment>().id;
		if (controlled_system.empty())
			return false;
	}
	bool authority = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller && ctrl.human != ai &&
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
	if (m.phase == AwayPhase::Hidden || m.revision >= 9007199254740991ULL || away_sequence >= 9007199254740991ULL ||
	    c.value != static_cast<std::int64_t>(m.revision))
		return false;
	const auto action = c.action.substr(5);
	if (action == "open")
	{
		if (!away_dispatch(a) || m.phase != AwayPhase::Available)
			return false;
		if (execute)
		{
			m.phase = AwayPhase::Open;
			++m.revision;
		}
		return true;
	}
	if (action == "send")
	{
		if (!away_dispatch(a) || (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) ||
		    !m.outcome.empty() || !away_staged(a.ship, m))
			return false;
		for (const auto &s : d.slots)
			if (s.required && !m.assignments.contains(s.id))
				return false;
		if (execute)
		{
			m.phase = AwayPhase::Outbound;
			m.sequence = ++away_sequence;
			m.transfer = 0;
			++m.revision;
		}
		return true;
	}
	if (action == "recall")
	{
		if (!away_dispatch(a) || m.phase != AwayPhase::Deployed || !away_blocker(index, false).empty())
			return false;
		if (execute)
		{
			m.phase = AwayPhase::Inbound;
			m.sequence = ++away_sequence;
			m.transfer = 0;
			++m.revision;
		}
		return true;
	}
	if (action == "cancel")
	{
		if (!away_dispatch(a) || (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
			return false;
		if (execute)
		{
			m.phase = m.deployed() ? AwayPhase::Deployed : AwayPhase::Open;
			m.transfer = 0;
			++m.revision;
		}
		return true;
	}
	if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return false;
	if (action == "transport")
	{
		if (!away_asset_owned(a, "transport:" + c.text))
			return false;
		if (execute)
		{
			m.transport = c.text;
			++m.revision;
		}
		return true;
	}
	if (action == "assign")
	{
		const auto separator = c.text.find('/');
		if (separator == std::string::npos)
			return false;
		const auto slot = c.text.substr(0, separator), asset = c.text.substr(separator + 1);
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
			refresh_personnel_bonuses(ps, pd);
			++ps.revision;
			++m.revision;
		}
		return true;
	}
	if (action == "withdraw")
	{
		if (!m.assignments.contains(c.text) || !away_asset_owned(a, m.assignments.at(c.text)))
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
	if (m.transport.empty() || m.deployed())
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
						auto route = compartment_route(pd.compartments.size(), pd.links, o.location, target);
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
		if (!ship || departed(ship) || m.phase == AwayPhase::Lost)
			continue;
		const auto cid = away_conversation(d);
		if (m.phase == AwayPhase::Hidden)
		{
			// A delivered introduction, not an undisclosed host event, makes a mission selectable.
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
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Outbound)
			stage_away(ship, m);
		if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound) && active.at({ship, m.transport}) == i)
			if (away_blocker(i).empty() && (m.deployed() || away_staged(ship, m)))
			{
				if (++m.transfer >= definition(ship).displacements.at(m.transport).ticks)
				{
					m.recovered = m.phase == AwayPhase::Inbound;
					m.phase = m.recovered ? AwayPhase::Recovered : AwayPhase::Deployed;
					if (!m.recovered)
					{
						m.ever_deployed = true;
						m.care.clear();
						for (const auto &[slot, a] : m.assignments)
							m.participants.insert(a);
						for (const auto &o : m.accompanying)
							m.participants.insert("officer:" + o);
					}
					m.transfer = 0;
					++m.revision;
					if (!m.deployed())
					{
						const auto location = away_origin(ship, m.transport);
						for (const auto &[slot, asset] : m.assignments)
							if (asset.starts_with("team:"))
							{
								auto &r =
								    away_equipment(ship, asset.substr(5)).get_mut<CombatMechanism>().get<TeamState>();
								r.location = location;
								r.destination = -1;
								r.edge_to = -1;
								r.travel = r.edge_total = 0;
								r.order.clear();
								r.activity.clear();
								r.patient.clear();
								r.continuation = {};
								r.repair_auto = false;
							}
						m.care.clear();
						m.assignments.clear();
						m.accompanying.clear();
						++entities.at(ship).get_mut<PersonnelState>().revision;
					}
				}
			}
		if (m.phase == AwayPhase::Deployed)
			advance_away_medical(i);
		if (m.outcome.empty())
		{
			if (m.deployed() && m.work > 0)
			{
				std::vector<flecs::entity> teams;
				std::int64_t specialist_bonus = 0, officer_work = 0;
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
								const auto bonus =
								    officer.bonuses.contains("repair") ? officer.bonuses.at("repair") : 0;
								specialist_bonus += bonus;
								officer_work += personnel_scale(1000, bonus);
							}
				const auto effort = structural_repair_work(entities.at(ship), teams, specialist_bonus);
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
			    m.progress >= m.work && repaired)
				mission_change("work/" + std::to_string(ship) + "/" + d.id + "/" + m.work_key, "completed",
				               "away.work_completed", ship, d.target, d.id, m.work_key);
			const auto answer = cid ? conversations.conversations.at(cid).prompt.answer : std::string{};
			using state_tree::Value;
			std::vector<Value> obs{Value::boolean(m.deployed()),
			                       Value::boolean(m.recovered),
			                       Value::boolean(m.work > 0 && m.progress >= m.work && repaired),
			                       Value::integer(tick),
			                       Value::integer(m.progress),
			                       Value::string(answer),
			                       Value::string(m.checkpoint)};
			const auto outputs = state_tree::step(d.program, m.continuation, obs);
			for (const auto &o : outputs.outputs)
			{
				if (!admit_effect(m.effects, o))
					continue;
				const auto &name = d.program.registry.actions.at(o.action).name;
				const auto &arg = o.arguments.at(0);
				if (name == "work")
				{
					if (arg.number < 1 || arg.number > 1728000)
						throw std::runtime_error("Invalid away work duration");
					if (m.work_key != m.checkpoint || m.work != arg.number * 1000)
					{
						m.work_key = m.checkpoint;
						m.work = arg.number * 1000;
						m.progress = 0;
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
					if (m.checkpoint != arg.text)
						m.repair_compartment.clear();
					m.checkpoint = arg.text;
				}
				else if (name == "report" && cid)
					conversations.event(cid, arg.text);
				else if (name == "complete" || name == "fail")
					m.outcome = name + ":" + arg.text;
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
		         {"result", m.phase == AwayPhase::Recovered ? m.outcome : std::string{}},
		         {"revision", m.revision},
		         {"transport", m.transport},
		         {"dispatch", away_dispatch(a)},
		         {"blocker", away_blocker(i)},
		         {"slots", Json::array()},
		         {"transports", Json::array()},
		         {"accompanying", m.accompanying}};
		if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
		{
			if (!away_staged(a.ship, m))
				row["blocker"] = "Personnel moving to displacement";
			for (const auto &slot : d.slots)
				if (slot.required && !m.assignments.contains(slot.id))
					row["blocker"] = "Mandatory assignments required";
		}
		for (const auto &[key, t] : definition(a.ship).displacements)
			if (m.outcome.empty() && away_asset_owned(a, "transport:" + key))
				row["transports"].push_back(key);
		for (const auto &s : d.slots)
		{
			const auto assigned = m.assignments.contains(s.id) ? m.assignments.at(s.id) : std::string{};
			Json r{{"id", s.id},
			       {"required", s.required},
			       {"assigned", assigned},
			       {"owned", !assigned.empty() && away_asset_owned(a, assigned)},
			       {"choices", Json::array()}};
			if (m.outcome.empty() && (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered))
			{
				for (const auto &duty : definition(a.ship).personnel.duties)
					if (!duty.team.empty())
					{
						const auto asset = "team:" + duty.team;
						if (team_owner(a.ship, duty.team) == a && away_asset_owned(a, asset) &&
						    !away_reserved(a.ship, asset) && away_asset_healthy(a.ship, asset) &&
						    away_slot_accepts(a.ship, s, asset))
							r["choices"].push_back(asset);
					}
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
		row["schema"] = 5;
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
	away_sequence = 0;
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = away_definitions[i];
		const auto schema = rows[i].at("schema");
		if ((schema != 1 && schema != 2 && schema != 3 && schema != 4 && schema != 5) || rows[i].at("id") != d.id)
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
		if (schema < 4)
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
		auto m = record.get<AwayMission>();
		if ((schema == 1 && rows[i].at("deployed").get<bool>() != m.deployed()) ||
		    (schema >= 2 && rows[i].contains("deployed")))
			throw std::runtime_error("Inconsistent saved deployment");
		if (!m.repair_compartment.empty() && !d.repair_compartments.contains(m.repair_compartment))
			throw std::runtime_error("Invalid saved repair target");
		const auto ship = away_ship(d);

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
		if (!m.transport.empty() && (!ship || !definition(ship).displacements.contains(m.transport)))
			throw std::runtime_error("Invalid saved transport");
		if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.deployed()) && m.transport.empty())
			throw std::runtime_error("Missing saved transport");
		if ((m.phase == AwayPhase::Recovered && !m.recovered) || m.accompanying.size() > 128 ||
		    m.checkpoint.size() > 256 || m.work_key.size() > 256 || m.outcome.size() > 512)
			throw std::runtime_error("Inconsistent away phase");
		if (!m.transport.empty() && m.transfer >= definition(ship).displacements.at(m.transport).ticks)
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
		m.continuation = state_tree::load(d.program, rows[i].at("continuation"));
		away_sequence = std::max(away_sequence, m.sequence);
		away_missions[i] = std::move(m);
	}
}
