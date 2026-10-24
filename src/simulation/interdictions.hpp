// Domain operation driven by an authored state tree. Flight, firing, docking and
// departure retain their ordinary Controller and physical execution paths.
std::vector<InterdictionDefinition> interdiction_definitions;
std::vector<InterdictionState> interdictions;
bool interdiction_docking_permission(EntityId ship, EntityId target) const
{
	for (std::size_t i = 0; i < interdictions.size(); ++i)
		if (interdiction_definitions[i].target == target && interdictions[i].boarder == ship &&
		    !interdictions[i].captured && !interdictions[i].finished &&
		    (interdictions[i].posture == "truce" || interdictions[i].posture == "hostile") && !interdictions[i].blocked)
			return true;
	return false;
}
void restore_interdiction_ships(const Json &rows, const Json &bodies)
{
	if (!rows.is_array() || rows.size() != interdiction_definitions.size())
		throw std::runtime_error("Interdiction roster mismatch");
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = interdiction_definitions[i];
		const auto fit = entities.at(slot_entities.at(d.slot)).get<Definition>().name;
		const auto &ships = d.variants.at(d.variants.contains(fit) ? fit : "default");
		const auto &ids = rows[i].at("ships");
		if (!ids.empty() && ids.size() != ships.size())
			throw std::runtime_error("Invalid interdiction fleet size");
		for (std::size_t n = 0; n < ids.size(); ++n)
		{
			const auto id = ids[n].get<EntityId>();
			const auto body = std::find_if(bodies.begin(), bodies.end(),
			                               [&](const auto &b)
			                               {
				                               return b.at("body").at("id") == id;
			                               });
			if (!id || entities.contains(id) || body == bodies.end() ||
			    body->at("body").at("definition") != ships[n].definition)
				throw std::runtime_error("Invalid interdiction ship identity");
			create(id, ships[n].definition, {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
		}
	}
}
void record_interdiction_attack(EntityId source, EntityId target)
{
	if (entities.contains(source) && entities.at(source).has<CraftState>())
		source = entities.at(source).get<CraftState>().home;
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		auto &s = interdictions[i];
		const auto player = slot_entities.at(interdiction_definitions[i].slot);
		if (operational_owner(source) == operational_owner(player) &&
		    std::find(s.ships.begin(), s.ships.end(), target) != s.ships.end())
			s.attacked = true;
	}
}
void record_interdiction_insertion(EntityId ship, EntityId target)
{
	for (std::size_t i = 0; i < interdictions.size(); ++i)
		if (interdictions[i].posture == "truce" && interdiction_definitions[i].target == target &&
		    ship == slot_entities.at(interdiction_definitions[i].slot))
			interdictions[i].reinforced = true;
}
bool encounter_command(EntityId ship, OperatingAction action, std::int64_t value)
{
	if (!entities.contains(ship) || !entities.at(ship).has<EntityControls>())
		return false;
	auto &controllers = entities.at(ship).get_mut<EntityControls>().controllers;
	bool accepted = false;
	for (std::size_t i = 0; i < controllers.size(); ++i)
	{
		auto &c = controllers[i];
		if (c.human)
			continue;
		for (std::size_t j = 0; j < c.definition.commands.size(); ++j)
			if (c.definition.commands[j].action == action)
			{
				if (action == OperatingAction::WeaponTarget && value)
				{
					const auto contacts = capture_sensor_contacts({ship, c.definition.console});
					if (std::none_of(contacts.begin(), contacts.end(),
					                 [&](const auto &contact)
					                 {
						                 return contact.id == static_cast<EntityId>(value);
					                 }))
						continue;
				}
				accepted |= commit_controller(
				    {{ship, c.definition.console, c.definition.id},
				     i,
				     j,
				     std::clamp(value, c.definition.commands[j].minimum, c.definition.commands[j].maximum),
				     c.generation,
				     0,
				     false,
				     {}});
			}
	}
	return accepted;
}
std::optional<fixed::Vec> encounter_contact(EntityId ship, EntityId target) const
{
	for (const auto &c : entities.at(ship).get<EntityControls>().controllers)
		if (!c.human)
			for (const auto &channel : c.definition.commands)
				if (channel.action == OperatingAction::Speed || channel.action == OperatingAction::Heading)
				{
					const auto contacts = capture_sensor_contacts({ship, c.definition.console});
					for (const auto &contact : contacts)
						if (contact.id == target)
							return fixed::Vec{contact.x, contact.y};
				}
	return {};
}
void encounter_move(EntityId ship, fixed::Vec destination, std::int64_t stop_radius, std::int64_t cruise_speed = 20000)
{
	if (entities.at(ship).has<DockConnection>())
		return;
	const auto delta = destination - entities.at(ship).get<Pose>().position;
	const auto distance = fixed::length(delta);
	const auto heading = distance > stop_radius ? combat_bearing(delta) : entities.at(ship).get<Pose>().heading;
	const auto difference = std::abs((heading - entities.at(ship).get<Pose>().heading + 540000) % 360000 - 180000);
	encounter_command(ship, OperatingAction::Heading, heading);
	encounter_command(ship, OperatingAction::Speed,
	                  difference < 5000 && distance > stop_radius
	                      ? std::min<std::int64_t>(cruise_speed, (distance - stop_radius) / 8)
	                      : 0);
}
bool encounter_dock_order(EntityId ship, const std::string &action, EntityId target = 0)
{
	if (action == "dock.undock" && entities.at(ship).has<DockConnection>() &&
	    entities.at(ship).get<DockConnection>().phase == "separating")
		return true;
	if (definition(ship).docking.empty())
		return false;
	const auto &port = definition(ship).docking.front();
	for (const auto &c : entities.at(ship).get<EntityControls>().controllers)
		if (!c.human && c.definition.id == port.controller)
		{
			OperatingCommand command{
			    ship, action, static_cast<std::int64_t>(entities.at(ship).get<DockingState>().revision), port.id};
			command.controller = c.definition.id;
			command.generation = c.generation;
			if (target)
			{
				if (definition(target).docking.empty())
					return false;
				command.text = std::to_string(target) + "/" + definition(target).docking.front().id;
			}
			return docking_command({ship, c.definition.console}, command, true, true);
		}
	return false;
}
void kill_original_complement(EntityId target, const std::vector<std::string> &occupation)
{
	auto e = entities.at(target);
	for (auto child : e.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
		{
			if (std::find(occupation.begin(), occupation.end(), child.get<Equipment>().id) == occupation.end())
				m->get<TeamState>().condition = "lost";
			else
			{
				m->get<TeamState>().condition = "healthy";
				m->get<TeamState>().detained = false;
				child.get_mut<Equipment>().enabled = true;
			}
		}
	if (e.has<PersonnelState>())
	{
		auto &p = e.get_mut<PersonnelState>();
		for (auto &o : p.officers)
		{
			o.condition = "dead";
			o.present = false;
			o.duty.clear();
			o.detained = false;
			o.casualty_tick = ecs.get<Clock>().tick;
			o.stun_until = o.treatment = o.recovery = o.remaining = 0;
			o.stabilised = false;
			o.edge_to = -1;
			o.facility.clear();
			o.escort.clear();
		}
		++p.revision;
	}
}
void restore_repairable_ship(EntityId target)
{
	auto &s = entities.at(target).get_mut<CombatStructure>();
	for (std::size_t i = 0; i < s.compartments.size(); ++i)
	{
		auto &c = s.compartments[i];
		apply_structural_repair(s, i, (s.definition.compartments[i].maximum - c.hp) * rate * 1000);
	}
	for (auto &[system, faults] : s.faults)
		for (std::size_t i = 0; i < faults.size(); ++i)
			finish_fault_repair(s, system, i);
}
void reset_interdiction_attempt(InterdictionState &s)
{
	s.progress = 0;
	if (s.conversation && conversations.conversations.contains(s.conversation))
		for (auto &message : conversations.conversations.at(s.conversation).messages)
			if (std::find(s.plea_messages.begin(), s.plea_messages.end(), message.id) != s.plea_messages.end())
				for (const auto &recipient : message.recipients)
					if (!message.delivered.contains(recipient))
						message.expired.insert(recipient);
	s.plea_messages.clear();
}
void advance_interdictions()
{
	using state_tree::Value;
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		auto &s = interdictions[i];
		const auto &d = interdiction_definitions[i];
		if (s.finished)
			continue;
		const auto player = slot_entities.at(d.slot);
		std::vector<EntityId> live;
		for (auto id : s.ships)
			if (!truly_destroyed(id) && !departed(id))
				live.push_back(id);
		if (live.empty() && s.boarder)
		{
			s.boarder = 0;
			if (!s.captured)
				reset_interdiction_attempt(s);
		}
		if (!live.empty() && std::find(live.begin(), live.end(), s.boarder) == live.end())
		{
			s.boarder = live.front();
			reset_interdiction_attempt(s);
		}
		const bool lost = truly_destroyed(d.target) || departed(d.target);
		const bool claimed = captured_by.contains(d.target) && operational_owner(d.target) == operational_owner(player);
		std::string answer;
		if (s.conversation && conversations.conversations.contains(s.conversation))
			answer = conversations.conversations.at(s.conversation).prompt.answer;
		const bool player_active = !truly_destroyed(player) && !departed(player);
		const bool close_threat =
		    s.boarder && player_active && s.posture == "hostile" &&
		    fixed::length(entities.at(player).get<Pose>().position - entities.at(s.boarder).get<Pose>().position) <=
		        (live.size() == 1 ? d.engagement
		         : s.blocked      ? d.clearance
		                          : d.threat);
		const bool ready = !lost && !claimed && !close_threat && s.boarder && !truly_destroyed(s.boarder) &&
		                   dock_ready(s.boarder, d.target) && s.progress + 1 >= d.takeover_ticks;
		const bool start = d.start_event.empty() || (scenario && !scenario->runtime().events(d.start_event).empty());
		std::vector<Value> observations{
		    Value::boolean(start), Value::boolean(s.attacked), Value::boolean(s.reinforced), Value::boolean(s.captured),
		    Value::boolean(lost),  Value::boolean(claimed),    Value::boolean(ready),        Value::string(answer)};
		const auto outputs = state_tree::step(d.program, s.continuation, observations);
		for (const auto &o : outputs.outputs)
		{
			if (!admit_effect(s.effects, o))
				continue;
			const auto name = d.program.registry.actions.at(o.action).name;
			const auto value = o.arguments.at(0).text;
			if (name == "spawn" && s.ships.empty())
			{
				const auto fit = entities.at(player).get<Definition>().name;
				const auto &ships = d.variants.at(d.variants.contains(fit) ? fit : "default");
				for (const auto &spawn : ships)
				{
					const auto id = next_entity++;
					create(id, spawn.definition,
					       {{"x_mm", spawn.x}, {"y_mm", spawn.y}, {"heading_mdeg", spawn.heading}});
					s.ships.push_back(id);
				}
				s.boarder = s.ships.front();
				const auto pirate = "ship:" + std::to_string(s.boarder);
				const auto engineer =
				    "ship:" + std::to_string(d.target) + (d.target_character.empty() ? "" : "/" + d.target_character);
				const auto self = "ship:" + std::to_string(player);
				s.conversation = conversations.open(
				    d.dialogue, {{"pirate", pirate}, {"engineer", engineer}, {"ship", self}}, {pirate, engineer, self});
			}
			else if (name == "posture")
			{
				s.posture = value;
				if (value != "watch" && s.conversation)
					conversations.conversations.at(s.conversation).prompt = {};
			}
			else if (name == "say" && s.conversation)
				conversations.report(s.conversation, value);
			else if (name == "offer" && s.conversation)
				conversations.offer(s.conversation, value);
			else if (name == "log" && s.effects.insert("log/" + value).second)
				mission_transition("mission.log", player, d.target, d.id, value);
			else if (name == "emit" && scenario)
				scenario->runtime().emit({conversation_instance, ++conversation_event_sequence, 0}, value,
				                         {{"ship", static_cast<std::int64_t>(player)}});
			else if (name == "finish")
			{
				s.finished = true;
				reset_interdiction_attempt(s);
			}
			else if (name == "takeover" && !s.captured && !lost && ready && s.boarder &&
			         dock_ready(s.boarder, d.target))
			{
				restore_repairable_ship(d.target);
				set_operational_owner(d.target, s.boarder);
				kill_original_complement(d.target, d.occupation_teams);
				capture_visitors(d.target, s.boarder);
				s.captured = true;
				s.progress = d.takeover_ticks;
				mission_transition("ship.taken", player, d.target, d.id, "taken_by_pirates");
			}
		}
		if (s.posture == "hostile" || s.posture == "truce")
		{
			if (!s.boarder || lost || claimed || truly_destroyed(s.boarder))
				continue;
			const auto separation =
			    fixed::length(entities.at(player).get<Pose>().position - entities.at(s.boarder).get<Pose>().position);
			const bool threat = player_active && s.posture == "hostile" &&
			                    separation <= (live.size() == 1 ? d.engagement
			                                   : s.blocked      ? d.clearance
			                                                    : d.threat);
			s.blocked = threat;
			if (threat || s.captured)
			{
				if (entities.at(s.boarder).has<DockConnection>())
					encounter_dock_order(s.boarder, "dock.undock");
				if (!s.captured)
					reset_interdiction_attempt(s);
			}
			else if (dock_ready(s.boarder, d.target))
			{
				s.progress = std::min(d.takeover_ticks, s.progress + 1);
				for (const auto &[tick, line] : d.pleas)
					if (s.progress == tick && s.conversation && conversations.report(s.conversation, line))
						s.plea_messages.push_back(conversations.conversations.at(s.conversation).messages.back().id);
			}
			else
			{
				reset_interdiction_attempt(s);
				if (!entities.at(s.boarder).has<DockConnection>())
					encounter_dock_order(s.boarder, "dock.assist", d.target);
			}
		}
	}
}
void charge_encounter_drive(EntityId ship)
{
	const auto drive = departure_drive(ship);
	if (!drive)
		return;
	encounter_command(ship, OperatingAction::GeneratorOutput, std::numeric_limits<std::int64_t>::max());
	auto &controls = entities.at(ship).get_mut<EntityControls>().controllers;
	for (std::size_t i = 0; i < controls.size(); ++i)
	{
		auto &c = controls[i];
		if (c.human)
			continue;
		for (std::size_t j = 0; j < c.definition.commands.size(); ++j)
			if (c.definition.commands[j].action == OperatingAction::SystemEnabled)
				for (auto consumer : c.definition.commands[j].consumers)
					if (definition(ship).fitted->values[consumer].id == drive.get<Equipment>().id)
						commit_controller(
						    {{ship, c.definition.console, c.definition.id}, i, j, 1, c.generation, 0, false, {}});
	}
}
void prepare_encounter_departure(EntityId leader, const std::set<EntityId> &group)
{
	for (const auto member : group)
	{
		charge_encounter_drive(member);
		if (entities.at(member).has<DockConnection>())
			encounter_dock_order(member, "dock.undock");
	}
	if (!departure_group_blocker(leader).empty())
		return;
	const auto drive = departure_drive(leader);
	if (!drive)
		return;
	const auto owner = ftl_owner(leader, drive.get<Equipment>().id);
	for (const auto &controller : entities.at(leader).get<EntityControls>().controllers)
		if (!controller.human && controller.definition.console == owner.console)
		{
			OperatingCommand command{leader, "ftl.depart", 0, drive.get<Equipment>().id};
			command.controller = controller.definition.id;
			command.generation = controller.generation;
			if (ftl_command(owner, command, true, true))
				return;
		}
}
void apply_interdiction_orders()
{
	prune_departure_groups();
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		auto &s = interdictions[i];
		const auto &d = interdiction_definitions[i];
		const auto player = slot_entities.at(d.slot);
		const auto target_position = entities.at(d.target).get<Pose>().position;
		if (s.captured && captured_by.contains(d.target) &&
		    std::find(s.ships.begin(), s.ships.end(), operational_owner(d.target)) != s.ships.end() &&
		    !truly_destroyed(d.target) && !departed(d.target))
		{
			std::set<EntityId> group{d.target};
			for (auto id : s.ships)
				if (!truly_destroyed(id) && !departed(id))
					group.insert(id);
			const auto leader = s.boarder ? s.boarder : d.target;
			if (group.contains(leader))
			{
				assign_departure_group(leader, group);
				const auto away = target_position - entities.at(player).get<Pose>().position;
				encounter_move(d.target, target_position + away, 10000);
				prepare_encounter_departure(leader, group);
			}
		}
		for (std::size_t n = 0; n < s.ships.size(); ++n)
		{
			const auto id = s.ships[n];
			if (truly_destroyed(id) || departed(id))
				continue;
			if (s.finished && entities.at(id).has<DockConnection>())
				encounter_dock_order(id, "dock.undock");
			const auto delta = entities.at(player).get<Pose>().position - entities.at(id).get<Pose>().position;
			const auto contact = encounter_contact(id, player);
			const bool attack = contact && s.posture == "hostile" && fixed::length(delta) <= d.engagement &&
			                    (id != s.boarder || s.blocked || s.captured);
			encounter_command(id, OperatingAction::WeaponTarget, attack ? player : 0);
			encounter_command(id, OperatingAction::GeneratorOutput, std::numeric_limits<std::int64_t>::max());
			if (id == s.boarder && entities.at(id).has<DockConnection>())
				continue;
			if (attack)
				encounter_move(id, *contact, d.combat_standoff, d.cruise_speed);
			else
			{
				const auto fit = entities.at(player).get<Definition>().name;
				const auto &variant = d.variants.at(d.variants.contains(fit) ? fit : "default");
				const auto offset = fixed::direction(variant[n].formation_bearing);
				encounter_move(id,
				               target_position + fixed::Vec{offset.x * d.formation / fixed::unit,
				                                            offset.y * d.formation / fixed::unit},
				               40000);
			}
		}
	}
	for (const auto &[leader, group] : departure_groups)
		for (const auto member : group)
			if (member != leader && !truly_destroyed(member) && !departed(member))
				charge_encounter_drive(member);
	// The same consumer used by ordinary Controller decisions actuates these admitted goals.
	for (const auto &[id, entity] : entities)
		if (!departed(id) && !truly_destroyed(id) && entity.has<EntityControls>())
			consume_controller_commands(entity);
}
Json save_interdictions() const
{
	Json rows = Json::array();
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		const auto &s = interdictions[i];
		rows.push_back({{"id", interdiction_definitions[i].id},
		                {"ships", s.ships},
		                {"boarder", s.boarder},
		                {"posture", s.posture},
		                {"conversation", s.conversation},
		                {"progress", s.progress},
		                {"attacked", s.attacked},
		                {"reinforced", s.reinforced},
		                {"captured", s.captured},
		                {"blocked", s.blocked},
		                {"finished", s.finished},
		                {"effects", s.effects},
		                {"plea_messages", s.plea_messages},
		                {"continuation", state_tree::save(interdiction_definitions[i].program, s.continuation)}});
	}
	return rows;
}
void restore_interdictions(const Json &rows)
{
	if (!rows.is_array() || rows.size() != interdictions.size())
		throw std::runtime_error("Invalid interdiction continuation");
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &j = rows[i];
		auto &s = interdictions[i];
		const auto &d = interdiction_definitions[i];
		if (j.at("id") != d.id)
			throw std::runtime_error("Interdiction identity mismatch");
		s.ships = j.at("ships").get<std::vector<EntityId>>();
		s.boarder = j.at("boarder");
		s.posture = j.at("posture");
		s.conversation = j.at("conversation");
		s.progress = j.at("progress");
		s.attacked = j.at("attacked");
		s.reinforced = j.at("reinforced");
		s.captured = j.at("captured");
		s.blocked = j.at("blocked");
		s.finished = j.at("finished");
		s.effects = j.at("effects").get<std::set<std::string>>();
		s.plea_messages = j.value("plea_messages", std::vector<std::uint64_t>{});
		if (s.ships.size() > 8 || s.progress < 0 || s.progress > d.takeover_ticks ||
		    s.effects.size() > effect_receipt_limit ||
		    (s.posture != "watch" && s.posture != "hostile" && s.posture != "truce") ||
		    (s.boarder && std::find(s.ships.begin(), s.ships.end(), s.boarder) == s.ships.end()) ||
		    (s.ships.empty() && (s.boarder || s.conversation || s.progress || s.captured)) ||
		    s.plea_messages.size() > d.pleas.size() ||
		    (s.conversation && !conversations.conversations.contains(s.conversation)))
			throw std::runtime_error("Invalid interdiction state");
		if ((s.captured && !captured_by.contains(d.target)) ||
		    (s.conversation && conversations.conversations.at(s.conversation).program != d.dialogue))
			throw std::runtime_error("Interdiction ownership or conversation receipt mismatch");
		for (const auto id : s.ships)
			if (!entities.contains(id))
				throw std::runtime_error("Unknown interdiction ship");
		for (const auto message : s.plea_messages)
			if (!s.conversation || std::none_of(conversations.conversations.at(s.conversation).messages.begin(),
			                                    conversations.conversations.at(s.conversation).messages.end(),
			                                    [&](const auto &m)
			                                    {
				                                    return m.id == message;
			                                    }))
				throw std::runtime_error("Unknown takeover plea receipt");
		s.continuation = state_tree::load(d.program, j.at("continuation"));
	}
}
void validate_interdiction_physics() const
{
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		const auto &s = interdictions[i];
		if (!s.captured && s.progress &&
		    (!s.boarder || s.blocked || !dock_ready(s.boarder, interdiction_definitions[i].target)))
			throw std::runtime_error("Takeover progress without eligible physical docking");
	}
}
