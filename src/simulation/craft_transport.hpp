// Finite carried entities share ordinary flight Controllers, resources, collision and damage.
std::vector<CarriedCraft> carried_craft;
bool transport_craft(EntityId id) const
{
	return std::any_of(carried_craft.begin(), carried_craft.end(),
	                   [&](const auto &c)
	                   {
		                   return c.id == id;
	                   });
}
void craft_active(const CarriedCraft &c, bool active)
{
	auto ship = entities.at(c.id);
	if (active)
		ship.enable();
	else
		ship.disable();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (active)
			child.enable();
		else
			child.disable();
}
void refresh_carried_craft()
{
	std::vector<EntityId> carriers;
	for (const auto &[id, entity] : entities)
		if (id < field_identity_base && !transport_craft(id))
			carriers.push_back(id);
	for (auto carrier : carriers)
		for (const auto &[key, t] : definition(carrier).displacements)
			if (t.mode != "displacement")
			{
				std::size_t count = 0;
				for (const auto &c : carried_craft)
					count += c.carrier == carrier && c.transport == key;
				while (count++ < static_cast<std::size_t>(t.count))
				{
					if (next_entity > 1000000000ULL)
						throw std::runtime_error("Craft identity space exhausted");
					const auto id = next_entity++;
					create(id, t.craft,
					       {{"x_mm", 0},
					        {"y_mm", 0},
					        {"heading_mdeg", 0},
					        {"faction", entities.at(carrier).get<FactionMembership>().id}});
					carried_craft.push_back({id, carrier, key});
					craft_active(carried_craft.back(), false);
				}
			}
}
void restore_craft_roster(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 256)
		throw std::runtime_error("Invalid carried-craft roster");
	const auto initial = carried_craft;
	carried_craft.clear();
	std::set<EntityId> ids;
	std::map<std::pair<EntityId, std::string>, std::size_t> counts;
	for (const auto &row : rows)
	{
		const auto c = row.get<CarriedCraft>();
		if (!c.id || c.id > 1000000000ULL || !ids.insert(c.id).second || !entities.contains(c.carrier) ||
		    c.id == c.carrier || !definition(c.carrier).displacements.contains(c.transport))
			throw std::runtime_error("Invalid carried-craft identity");
		const auto &t = definition(c.carrier).displacements.at(c.transport);
		if (t.mode == "displacement" || ++counts[{c.carrier, c.transport}] > static_cast<std::size_t>(t.count) ||
		    c.mission.size() > 64 ||
		    (c.phase != "stored" && c.phase != "outbound" && c.phase != "pickup" && c.phase != "inbound" &&
		     c.phase != "docked" && c.phase != "return_empty" && c.phase != "consumed" && c.phase != "lost"))
			throw std::runtime_error("Invalid carried-craft phase/count");
		if (entities.contains(c.id))
		{
			if (std::none_of(initial.begin(), initial.end(),
			                 [&](const auto &i)
			                 {
				                 return i.id == c.id && i.carrier == c.carrier && i.transport == c.transport;
			                 }))
				throw std::runtime_error("Craft overlaps another entity");
		}
		else
			create(c.id, t.craft,
			       {{"x_mm", 0},
			        {"y_mm", 0},
			        {"heading_mdeg", 0},
			        {"faction", entities.at(c.carrier).get<FactionMembership>().id}});
		carried_craft.push_back(c);
		craft_active(c, c.phase != "stored" && c.phase != "docked" && c.phase != "consumed" && c.phase != "lost");
	}
	for (auto id : ids)
		if (id >= next_entity)
			throw std::runtime_error("Craft overlaps entity allocator");
	for (const auto &[carrier, entity] : entities)
		if (carrier < field_identity_base && !transport_craft(carrier))
			for (const auto &[key, t] : definition(carrier).displacements)
				if (t.mode != "displacement" && counts[{carrier, key}] != static_cast<std::size_t>(t.count))
					throw std::runtime_error("Missing finite craft");
}
std::string craft_transport_blocker(std::size_t index) const
{
	const auto &d = away_definitions[index];
	const auto &m = away_missions[index];
	const auto origin = away_ship(d);
	const auto &t = definition(origin).displacements.at(m.transport);
	if (t.mode == "boarding_torpedo" && (m.phase == AwayPhase::Inbound || m.phase == AwayPhase::Deployed))
		return "Boarding torpedo cannot recover personnel";
	const auto bay = away_equipment(origin, m.transport);
	if (combat_dead(entities.at(origin)) || !bay.get<Equipment>().enabled ||
	    structurally_disabled(entities.at(origin), m.transport) ||
	    !fault_effectiveness(entities.at(origin), m.transport))
		return "Transport disabled";
	if (m.assignments.size() + m.accompanying.size() > static_cast<std::size_t>(t.capacity))
		return "Transport capacity exceeded";
	const bool returning = m.phase == AwayPhase::Inbound && craft_passengers_extracted(m);
	if (!returning && (!entities.contains(d.target) || combat_dead(entities.at(d.target)) || departed(d.target)))
		return "Destination unavailable";
	if (!m.craft &&
	    fixed::length(entities.at(origin).get<Pose>().position - entities.at(d.target).get<Pose>().position) > t.range)
		return "Out of transport range";
	if (d.boarding &&
	    (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Outbound) &&
	    t.mode == "shuttle")
	{
		const auto &structure = entities.at(d.target).get<CombatStructure>();
		std::int64_t hp = 0, maximum = 0;
		for (std::size_t i = 0; i < structure.compartments.size(); ++i)
		{
			hp += structure.compartments[i].hp;
			maximum += structure.definition.compartments[i].maximum;
		}
		if (hp * 2 >= maximum)
			return "Shuttle boarding requires hull below 50%";
	}
	if (!m.craft)
	{
		bool available = false;
		for (const auto &c : carried_craft)
			available |= c.carrier == origin && c.transport == m.transport &&
			             (c.phase == "stored" || c.phase == "docked" && c.mission == d.id);
		if (!available)
			return "No available transport craft";
	}
	return {};
}
void launch_craft(CarriedCraft &c, EntityId destination)
{
	const auto carrier = entities.at(c.carrier);
	auto ship = entities.at(c.id);
	const auto &from = carrier.get<Pose>();
	const auto delta = entities.at(destination).get<Pose>().position - from.position;
	const auto direction = fixed::normal(delta);
	const auto &a = definition(c.carrier).collider;
	const auto &b = definition(c.id).collider;
	const auto clearance =
	    (a.box ? a.half_width + a.half_length : a.radius) + (b.box ? b.half_width + b.half_length : b.radius) + 1000;
	auto &pose = ship.get_mut<Pose>();
	pose.position = {from.position.x + direction.x * clearance / fixed::unit,
	                 from.position.y + direction.y * clearance / fixed::unit};
	pose.heading = combat_bearing(delta);
	ship.get_mut<Motion>().velocity = carrier.get<Motion>().velocity;
	craft_active(c, true);
}
void lose_craft_party(std::size_t index)
{
	auto &m = away_missions[index];
	const auto origin = away_ship(away_definitions[index]);
	for (const auto &[slot, asset] : m.assignments)
	{
		mission_ledger.lost[origin].insert(asset);
		if (asset.starts_with("team:"))
		{
			auto &r = away_equipment(origin, asset.substr(5)).get_mut<CombatMechanism>().get<TeamState>();
			r.condition = "lost";
			r.treatment = 0;
			++r.revision;
		}
	}
	const auto &pd = definition(origin).personnel;
	auto &ps = entities.at(origin).get_mut<PersonnelState>();
	for (std::size_t i = 0; i < ps.officers.size(); ++i)
	{
		bool aboard =
		    std::find(m.accompanying.begin(), m.accompanying.end(), pd.officers[i].id) != m.accompanying.end();
		for (const auto &[slot, asset] : m.assignments)
			aboard |= asset == "officer:" + pd.officers[i].id;
		if (aboard)
		{
			mission_ledger.lost[origin].insert("officer:" + pd.officers[i].id);
			auto &o = ps.officers[i];
			o.condition = "dead";
			o.duty.clear();
			o.present = false;
			o.stun_until = o.treatment = o.recovery = 0;
			o.stabilised = false;
			o.facility.clear();
			o.escort.clear();
			o.remaining = 0;
			o.edge_to = -1;
		}
	}
	personnel_changed(origin);
	m.assignments.clear();
	m.accompanying.clear();
	m.care.clear();
	m.phase = AwayPhase::Lost;
	m.transfer = 0;
	m.craft = 0;
	m.outcome = "fail:Transport destroyed";
	++m.revision;
}
void guide_carried_craft()
{
	for (auto &c : carried_craft)
	{
		if (c.phase != "outbound" && c.phase != "pickup" && c.phase != "inbound" && c.phase != "return_empty")
			continue;
		if (combat_dead(entities.at(c.id)))
		{
			const bool empty_pickup = c.phase == "pickup";
			c.phase = "lost";
			craft_active(c, false);
			for (std::size_t i = 0; i < away_definitions.size(); ++i)
				if (away_definitions[i].id == c.mission && away_ship(away_definitions[i]) == c.carrier &&
				    away_missions[i].craft == c.id)
				{
					if (empty_pickup)
					{
						auto &m = away_missions[i];
						m.phase = AwayPhase::Deployed;
						m.craft = 0;
						m.transfer = 0;
						++m.revision;
					}
					else
						lose_craft_party(i);
				}
			continue;
		}
		EntityId destination = c.carrier;
		if (c.phase == "outbound" || c.phase == "pickup")
			for (const auto &d : away_definitions)
				if (d.id == c.mission && away_ship(d) == c.carrier)
					destination = d.target;
		if (!entities.contains(destination) || departed(destination) || combat_dead(entities.at(destination)))
			continue;
		const auto &t = definition(c.carrier).displacements.at(c.transport);
		if (c.phase == "return_empty" && craft_arrived(c, destination))
		{
			c.phase = "stored";
			c.mission.clear();
			craft_active(c, false);
			continue;
		}
		auto &controls = entities.at(c.id).get_mut<EntityControls>();
		for (std::size_t i = 0; i < controls.controllers.size(); ++i)
		{
			const auto &controller = controls.controllers[i];
			if (controller.human)
				continue;
			for (std::size_t j = 0; j < controller.definition.commands.size(); ++j)
				if (controller.definition.commands[j].action == OperatingAction::FollowContact)
					commit_controller({{c.id, controller.definition.console, controller.definition.id},
					                   i,
					                   j,
					                   1,
					                   controller.generation,
					                   0,
					                   false,
					                   {static_cast<std::int64_t>(destination),
					                    std::max<std::int64_t>(1, t.arrival_distance / 2), t.cruise_speed}});
		}
	}
}
bool craft_arrived(const CarriedCraft &c, EntityId destination) const
{
	const auto &t = definition(c.carrier).displacements.at(c.transport);
	const auto craft = entities.at(c.id), target = entities.at(destination);
	return fixed::length(craft.get<Pose>().position - target.get<Pose>().position) <= t.arrival_distance &&
	       fixed::length(craft.get<Motion>().velocity - target.get<Motion>().velocity) <= t.arrival_speed;
}
bool craft_transfer_ready(std::size_t index)
{
	auto &m = away_missions[index];
	const auto &d = away_definitions[index];
	const auto origin = away_ship(d);
	const auto &t = definition(origin).displacements.at(m.transport);
	if (!craft_transport_blocker(index).empty())
		return false;
	if (!m.craft)
	{
		for (auto &c : carried_craft)
			if (c.carrier == origin && c.transport == m.transport &&
			    (c.phase == "stored" || (c.phase == "docked" && c.mission == d.id)))
			{
				if (c.phase == "stored")
				{
					if (m.phase == AwayPhase::Outbound && !away_staged(origin, m))
						return false;
					launch_craft(c, d.target);
					c.phase = m.phase == AwayPhase::Outbound ? "outbound" : "pickup";
				}
				else
				{
					craft_active(c, true);
					c.phase = "inbound";
				}
				c.mission = d.id;
				m.craft = c.id;
				break;
			}
	}
	if (!m.craft)
		return false;
	for (auto &c : carried_craft)
		if (c.id == m.craft)
		{
			if (c.phase == "pickup")
			{
				if (!craft_arrived(c, d.target))
					return false;
				c.phase = "inbound";
				return false;
			}
			return craft_arrived(c, c.phase == "inbound" ? origin : d.target);
		}
	return false;
}
void finish_craft_transfer(std::size_t index)
{
	auto &m = away_missions[index];
	if (!m.craft)
		return;
	const auto &d = away_definitions[index];
	for (auto &c : carried_craft)
		if (c.id == m.craft)
		{
			if (m.phase == AwayPhase::Inbound)
			{
				c.phase = "stored";
				c.mission.clear();
				craft_active(c, false);
			}
			else if (definition(c.carrier).displacements.at(c.transport).mode == "boarding_torpedo")
			{
				c.phase = "consumed";
				c.mission.clear();
				craft_active(c, false);
			}
			else if (!d.retain_craft)
			{
				c.phase = "return_empty";
				c.mission.clear();
			}
			else
			{
				c.phase = "docked";
				craft_active(c, false);
			}
			m.craft = 0;
			return;
		}
}
void release_docked_craft(std::size_t index)
{
	for (auto &c : carried_craft)
		if (c.phase == "docked" && c.mission == away_definitions[index].id)
		{
			c.phase = "return_empty";
			c.mission.clear();
			craft_active(c, true);
		}
}
void cancel_craft_transfer(std::size_t index)
{
	auto &m = away_missions[index];
	for (auto &c : carried_craft)
		if (c.id == m.craft)
		{
			if (definition(c.carrier).displacements.at(c.transport).mode == "boarding_torpedo")
			{
				// Only destination destruction reaches this path: launched torpedoes never return.
				c.phase = "consumed";
				c.mission.clear();
				craft_active(c, false);
				lose_craft_party(index);
				m.outcome = "fail:Boarding destination destroyed";
				return;
			}
			c.phase = "inbound";
			m.phase = AwayPhase::Inbound;
			m.returning_without_delivery = true;
			m.transfer = 0;
			++m.revision;
		}
}
bool craft_passengers_extracted(const AwayMission &m) const
{
	if (!m.craft)
		return false;
	for (const auto &c : carried_craft)
		if (c.id == m.craft)
			return c.phase == "inbound";
	return false;
}
void validate_craft_missions() const
{
	std::set<EntityId> assigned;
	for (const auto &c : carried_craft)
		if (definition(c.carrier).displacements.at(c.transport).mode == "boarding_torpedo" && c.phase != "stored" &&
		    c.phase != "outbound" && c.phase != "consumed" && c.phase != "lost")
			throw std::runtime_error("Boarding torpedoes cannot return or recover personnel");
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &m = away_missions[i];
		if (m.returning_without_delivery && m.phase != AwayPhase::Inbound)
			throw std::runtime_error("Invalid aborted transport");
		if (m.craft)
		{
			if (!assigned.insert(m.craft).second || (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
				throw std::runtime_error("Invalid craft assignment");
			bool match = false;
			for (const auto &c : carried_craft)
				match |= c.id == m.craft && c.carrier == away_ship(away_definitions[i]) &&
				         c.mission == away_definitions[i].id && c.transport == m.transport &&
				         (c.phase == "outbound" || c.phase == "pickup" || c.phase == "inbound");
			if (!match)
				throw std::runtime_error("Orphaned craft transport");
		}
	}
	for (const auto &c : carried_craft)
	{
		if ((c.phase == "stored" || c.phase == "consumed" || c.phase == "return_empty") && !c.mission.empty())
			throw std::runtime_error("Unexpected craft commitment");
		if (c.phase == "docked")
		{
			bool match = false;
			for (std::size_t i = 0; i < away_missions.size(); ++i)
				match |= away_definitions[i].id == c.mission && away_ship(away_definitions[i]) == c.carrier &&
				         away_missions[i].transport == c.transport && away_missions[i].phase == AwayPhase::Deployed;
			if (!match)
				throw std::runtime_error("Orphaned docked craft");
		}
		if (c.phase == "outbound" || c.phase == "pickup" || c.phase == "inbound")
			if (!assigned.contains(c.id))
				throw std::runtime_error("Unclaimed flying transport");
	}
}
