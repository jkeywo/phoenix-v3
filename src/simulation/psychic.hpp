// World adapter. Apparatus components own commitments; receiver records own apparent evidence.
std::map<EntityId, GhostReception> ghost_receptions;
std::map<EntityId, std::int64_t> concealment_cache;
std::map<EntityId, std::uint64_t> weapon_emissions;
void psychic_weapon_emission(EntityId ship)
{
	weapon_emissions[ship] = ecs.get<Clock>().tick + 20;
}
std::int64_t emission(EntityId ship) const
{
	std::int64_t result = 0;
	const auto entity = entities.at(ship);
	for (auto child : entity.get<EquipmentIndex>().values)
	{
		const auto &e = child.get<Equipment>();
		if (!e.enabled || structurally_disabled(entity, e.id))
			continue;
		std::int64_t funded = 1000;
		if (const auto *p = entity.try_get<ShipResources>(); p && child.has<ResourceBinding>())
			funded = p->grants.at(child.get<ResourceBinding>().slot).supply_permille;
		if (e.kind == OperatingSystem::Generator)
			if (const auto *power = entity.try_get<ShipResources>(); power && child.has<ResourceBinding>())
			{
				const auto generator = power->definition.systems[child.get<ResourceBinding>().slot].generator;
				if (generator != no_resource)
					result += std::min<std::int64_t>(
					    200,
					    power->network.generators()[generator].actual_rate * 200 /
					        std::max<std::int64_t>(1, power->definition.network.generators[generator].maximum_rate));
			}
		if (child.has<Engine>())
			result += std::abs(child.get<Engine>().forward_output) * funded / 2000;
		if (child.has<Radar>() && child.get<Radar>().enabled && child.get<SensorRuntime>().target)
			result += funded / 4;
	}
	if (weapon_emissions.contains(ship) && weapon_emissions.at(ship) > ecs.get<Clock>().tick)
		result += 500;
	for (const auto &[id, c] : conversations.conversations)
		for (const auto &m : c.messages)
			if (m.sender == "ship:" + std::to_string(ship) && m.sent + 20 >= ecs.get<Clock>().tick)
				result += 250;
	return std::min<std::int64_t>(1000, result);
}
const OfficerDefinition *choir_officer(EntityId ship, const std::string &duty) const
{
	const auto &p = definition(ship).personnel;
	const auto &s = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < s.officers.size(); ++i)
		if (s.officers[i].duty == duty && s.officers[i].present && officer_healthy(s.officers[i]))
			return &p.officers[i];
	return nullptr;
}
bool choir_assignment(EntityId ship, const OfficerDefinition &officer, const PersonnelDuty *duty) const
{
	return !duty || duty->choir.empty() ||
	       (!officer.power.empty() && definition(ship).psychic.choirs.at(duty->choir).powers.contains(officer.power));
}
using PsychicContacts = std::map<EntityId, BodyView>;
PsychicContacts index_psychic_contacts(const std::vector<BodyView> &contacts) const
{
	PsychicContacts result;
	for (const auto &c : contacts)
		result.emplace(c.id, c);
	return result;
}
bool psychic_target_valid(EntityId ship, const ChoirSeatDefinition &binding, EntityId target,
                          const PsychicContacts &contacts) const
{
	if (!target)
		return true;
	const auto *o = choir_officer(ship, binding.duty);
	const auto kind = o ? definition(ship).psychic.powers.at(o->power).kind : "";
	if (target == ship)
		return kind != "scramble";
	if (!entities.contains(target) || departed(target) || combat_dead(entities.at(target)))
		return false;
	const auto found = contacts.find(target);
	return found != contacts.end() && (kind != "ward" || found->second.disposition == "friendly");
}
bool psychic_channel_valid(EntityId ship, std::size_t controller, std::size_t channel, std::int64_t value) const
{
	for (const auto &[id, c] : definition(ship).psychic.choirs)
		for (const auto &b : c.seats)
			if ((b.controller == controller && b.target == channel) ||
			    (b.planner == controller && b.planning_target == channel))
			{
				const auto &owner = entities.at(ship).get<EntityControls>().controllers.at(controller);
				const auto contacts = index_psychic_contacts(capture_sensor_contacts({ship, owner.definition.console}));
				return psychic_target_valid(ship, b, value, contacts);
			}
	return true;
}
void clear_choir_intent(EntityId ship, const std::string &duty)
{
	for (const auto &[id, c] : definition(ship).psychic.choirs)
		for (std::size_t i = 0; i < c.seats.size(); ++i)
			if (c.seats[i].duty == duty)
			{
				const auto &b = c.seats[i];
				clear_controller_settings(ship, b.controller, {b.enabled, b.target});
				if (b.planner)
					clear_controller_settings(ship, *b.planner, {b.planning_enabled, b.planning_target});
				auto &s = instance(entities.at(ship), id)->entity.get_mut<PsychicChoirState>().seats[i];
				s.active = false;
				s.work = 0;
			}
}
void prepare_psychic(EntityId ship)
{
	std::map<std::string, PsychicContacts> snapshots;
	const auto tick = ecs.get<Clock>().tick;
	for (const auto &[id, c] : definition(ship).psychic.choirs)
	{
		auto child = instance(entities.at(ship), id)->entity;
		auto &states = child.get_mut<PsychicChoirState>().seats;
		for (std::size_t i = 0; i < c.seats.size(); ++i)
		{
			const auto &b = c.seats[i];
			auto &s = states[i];
			s.work = 0;
			const auto &controller = entities.at(ship).get<EntityControls>().controllers.at(b.controller);
			const auto *o = choir_officer(ship, b.duty);
			const bool requested = controller.commands[b.enabled].value && o && c.powers.contains(o->power);
			const EntityId target = controller.commands[b.target].value;
			if (!requested || s.officer != (o ? o->id : "") || s.target != target)
			{
				if (s.active)
				{
					s.active = false;
					s.ready = tick + seat_power(ship, s).recovery_ticks;
				}
			}
			if (!requested)
				continue;
			const auto &p = definition(ship).psychic.powers.at(o->power);
			s.officer = o->id;
			const auto &console = controller.definition.console;
			if (!snapshots.contains(console))
				snapshots.emplace(console, index_psychic_contacts(capture_sensor_contacts({ship, console})));
			const auto &contacts = snapshots.at(console);
			const auto observed = contacts.find(target);
			if (observed != contacts.end())
			{
				s.aim_x = observed->second.x;
				s.aim_y = observed->second.y;
			}

			if (!s.active && tick >= s.ready && (p.kind == "cloak" || target) &&
			    psychic_target_valid(ship, b, target, contacts))
			{
				s.active = true;
				s.target = target;
				s.activation = tick;
				s.shapes.clear();
				if (p.kind == "scramble" && target != ship)
				{
					const auto &pose = entities.at(target).get<Pose>();
					for (std::int64_t g = 0; g < p.ghosts; ++g)
					{
						PsychicGhostShape shape;
						shape.pose.position.x = s.aim_x + p.offset;
						shape.pose.position.y = s.aim_y + (g - (p.ghosts - 1) / 2) * p.spacing;
						shape.pose.heading = pose.heading;
						shape.pose.altitude = pose.altitude;
						s.shapes.push_back(shape);
					}
				}
			}
		}
	}
}
void request_psychic_resources(EntityId ship)
{
	auto entity = entities.at(ship);
	auto *power = entity.try_get_mut<ShipResources>();
	if (!power)
		return;
	for (const auto &[id, c] : definition(ship).psychic.choirs)
	{
		auto child = instance(entity, id)->entity;
		std::int64_t demand = 0;
		if (child.get<Equipment>().enabled && !structurally_disabled(entity, id))
			for (const auto &s : child.get<PsychicChoirState>().seats)
				if (s.active)
				{
					const auto *o = choir_officer(ship, c.seats[&s - child.get<PsychicChoirState>().seats.data()].duty);
					if (o)
						demand += definition(ship).psychic.powers.at(o->power).energy;
				}
		request_system_resource(*power, child.get<ResourceBinding>().slot, 0, false, demand);
	}
}
void refresh_psychic_work()
{
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		const auto &c = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
		auto &state = child.get_mut<PsychicChoirState>();
		std::int64_t funded = 0;
		if (const auto *power = entities.at(ship).try_get<ShipResources>(); power && child.has<ResourceBinding>())
			funded = power->grants.at(child.get<ResourceBinding>().slot).supply_permille;
		for (std::size_t i = 0; i < state.seats.size(); ++i)
		{
			auto &s = state.seats[i];
			const auto *o = choir_officer(ship, c.seats[i].duty);
			s.work = s.active && o && o->id == s.officer && child.get<Equipment>().enabled && !departed(ship) &&
			                 !structurally_disabled(entities.at(ship), c.id)
			             ? definition(ship).psychic.powers.at(o->power).strength * funded / 1000 *
			                   fault_effectiveness(entities.at(ship), c.id) / 1000
			             : 0;
		}
	}
}
const PsychicPowerDefinition &seat_power(EntityId ship, const PsychicSeatState &s) const
{
	const auto &d = definition(ship);
	const auto o = std::find_if(d.personnel.officers.begin(), d.personnel.officers.end(),
	                            [&](const auto &v)
	                            {
		                            return v.id == s.officer;
	                            });
	return d.psychic.powers.at(o->power);
}
bool psychic_coverage(EntityId ship, const ChoirDefinition &c, const PsychicSeatState &s, EntityId receiver) const
{
	if (!s.work || !entities.contains(receiver) || departed(receiver) || combat_dead(entities.at(receiver)))
		return false;
	if (receiver == ship)
		return true;
	if (!entities.contains(s.target))
		return false;
	const auto origin = entities.at(ship).get<Pose>().position;
	const auto delta = entities.at(receiver).get<Pose>().position - origin;
	const auto aim = fixed::Vec{s.aim_x, s.aim_y} - origin;
	return distance_3d(ship, receiver) <= c.range &&
	       scenario_sensor_visible(regions, {origin.x, origin.y}, {origin.x + delta.x, origin.y + delta.y}, c.range) &&
	       (c.arc == 360000 || std::abs(combat_delta(combat_bearing(delta), combat_bearing(aim))) <= c.arc / 2);
}
std::int64_t compute_concealment(EntityId target) const
{
	std::int64_t strength = 0;
	if (!entities.contains(target))
		return 0;
	for (const auto &[id, c] : definition(target).psychic.choirs)
		for (const auto &s : instance(entities.at(target), id)->entity.get<PsychicChoirState>().seats)
			if (s.work && seat_power(target, s).kind == "cloak")
				strength = std::max(strength, s.work);
	return strength ? strength * (1000 - emission(target)) / 1000 : 0;
}
void refresh_concealment()
{
	concealment_cache.clear();
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			concealment_cache[ship] = compute_concealment(ship);
}
std::int64_t concealment(EntityId target) const
{
	const auto value = concealment_cache.find(target);
	return value == concealment_cache.end() ? 0 : value->second;
}
bool psychic_detects(EntityId observer, const std::string &sensor, EntityId target) const
{
	return distance_3d(observer, target) <= sensor_range(observer, sensor) * (1000 - concealment(target)) / 1000;
}
void dispel_ghost(GhostReception &g)
{
	if (g.dispelled)
		return;
	g.dispelled = true;
	if (g.ended < 0)
		g.ended = ecs.get<Clock>().tick;
	record_sensor_dispel(g);
}
bool ghost_live(const GhostReception &g) const
{
	return !g.dispelled && g.ended < 0 && g.seen == ecs.get<Clock>().tick;
}
using PsychicSource = std::tuple<EntityId, std::string, std::size_t>;
std::map<EntityId, PsychicSource> scrambler_winners;
void refresh_psychic_reception()
{
	scrambler_winners.clear();
	const auto choirs = ordered_equipment(choir_query);
	for (const auto &[receiver, entity] : entities)
	{
		if (!entity.has<EquipmentIndex>())
			continue;
		std::int64_t strength = 0;
		PsychicSource winner{};
		for (bool wards : {true, false})
			for (auto child : choirs)
			{
				const auto ship = child.parent().get<StableId>().id;
				const auto &c = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
				const auto &seats = child.get<PsychicChoirState>().seats;
				for (std::size_t i = 0; i < seats.size(); ++i)
				{
					const auto &s = seats[i];
					if (s.work <= strength)
						continue;
					const bool ward = seat_power(ship, s).kind == "ward";
					if (ward != wards || (ward ? s.target != receiver : ship == receiver || s.shapes.empty()) ||
					    !psychic_coverage(ship, c, s, receiver))
						continue;
					strength = s.work;
					if (!ward)
						winner = {ship, c.id, i};
				}
			}
		scrambler_winners.emplace(receiver, winner);
	}
}
PsychicSource strongest_scrambler(EntityId receiver) const
{
	const auto found = scrambler_winners.find(receiver);
	return found == scrambler_winners.end() ? PsychicSource{} : found->second;
}
void advance_psychic()
{
	refresh_psychic_work();
	refresh_psychic_reception();
	refresh_concealment();
	const auto tick = ecs.get<Clock>().tick;
	std::erase_if(weapon_emissions,
	              [&](const auto &v)
	              {
		              return !entities.contains(v.first) || v.second <= tick;
	              });
	for (auto child : ordered_equipment(choir_query))
		if (child.has<PsychicChoirState>())
		{
			const auto ship = child.parent().get<StableId>().id;
			const auto &c = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
			auto &states = child.get_mut<PsychicChoirState>().seats;
			for (std::size_t i = 0; i < states.size(); ++i)
			{
				auto &s = states[i];
				if (!s.active || !s.work || s.shapes.empty())
					continue;
				const auto &p = seat_power(ship, s);
				for (auto &g : s.shapes)
				{
					Engine engine{p.speed, p.acceleration, p.speed, g.acceleration_remainder};
					Steering steer{p.turn_rate, combat_angle(g.pose.heading + p.turn_rate / rate), g.turn_remainder};
					step_basic_steering(g.pose, steer, 1000);
					step_basic_engine(g.pose, g.motion, engine, 1000);
					integrate_motion(g.pose, g.motion);
					g.acceleration_remainder = engine.remainder;
					g.turn_remainder = steer.remainder;
				}
				for (const auto &[receiver, entity] : entities)
				{
					if (receiver == ship || !entity.has<EquipmentIndex>() ||
					    strongest_scrambler(receiver) != std::tuple{ship, c.id, i})
						continue;
					for (std::size_t n = 0; n < s.shapes.size(); ++n)
					{
						const auto &shape = s.shapes[n];
						bool visible = false;
						for (const auto &[sensor, d] : definition(receiver).sensors)
							if (sensor_covers(receiver, sensor, {shape.pose.position.x, shape.pose.position.y}))
								visible = true;
						if (!visible)
							continue;
						auto found = std::find_if(ghost_receptions.begin(), ghost_receptions.end(),
						                          [&](const auto &entry)
						                          {
							                          const auto &g = entry.second;
							                          return g.observer == receiver && g.source == ship &&
							                                 g.choir == c.id && g.seat == i &&
							                                 g.activation == s.activation && g.member == n;
						                          });
						if (found == ghost_receptions.end())
						{
							if (ghost_receptions.size() >= 4096 || next_entity > 1000000000ULL)
								continue;
							GhostReception g;
							g.id = next_entity++;
							g.observer = receiver;
							g.source = ship;
							g.choir = c.id;
							g.seat = i;
							g.member = n;
							g.activation = s.activation;
							g.power = p.id;
							found = ghost_receptions.emplace(g.id, g).first;
						}
						auto &g = found->second;
						if (g.dispelled)
							continue;
						g.last = shape;
						g.seen = tick;
						g.ended = -1;
					}
				}
			}
		}
	for (auto &[id, g] : ghost_receptions)
		if (g.seen != tick && g.ended < 0)
			g.ended = tick;
	// Keep rejection receipts for the current activation. Old activations expire after their fade.
	std::erase_if(
	    ghost_receptions,
	    [&](const auto &entry)
	    {
		    const auto &g = entry.second;
		    if (!entities.contains(g.source) || !definition(g.source).psychic.choirs.contains(g.choir))
			    return true;
		    const auto &s =
		        instance(entities.at(g.source), g.choir)->entity.template get<PsychicChoirState>().seats[g.seat];
		    return g.ended >= 0 && tick > static_cast<std::uint64_t>(g.ended) + 200 &&
		           (!s.active || s.activation != g.activation);
	    });
}
BodyView ghost_body(const GhostReception &g) const
{
	BodyView b;
	b.id = g.id;
	b.definition = definition(g.source).psychic.powers.at(g.power).apparent_class;
	b.x = g.last.pose.position.x;
	b.y = g.last.pose.position.y;
	b.vx = g.last.motion.velocity.x;
	b.vy = g.last.motion.velocity.y;
	b.heading = g.last.pose.heading;
	b.altitude = g.last.pose.altitude;
	b.radar_icon = RadarIcon::ship;
	b.disposition = "hostile";
	b.faction_label = "Unknown allegiance";
	return b;
}
void ghost_damage(EntityId observer, fixed::Vec from, fixed::Vec to, std::int64_t from_z, std::int64_t to_z,
                  std::int64_t radius = 0)
{
	for (auto &[id, g] : ghost_receptions)
		if (g.observer == observer && ghost_live(g))
		{
			const auto &p = definition(g.source).psychic.powers.at(g.power);
			CollisionBody body;
			body.position = {g.last.pose.position.x, g.last.pose.position.y};
			body.altitude = g.last.pose.altitude;
			body.radius = p.radius;
			if (volume_hit(from, to, from_z, to_z, body, body.position, g.last.pose.altitude, g.last.pose.altitude,
			               p.radius, radius))
				dispel_ghost(g);
		}
}
bool psychic_controls(const ConsoleAddress &address, const ConsoleControllerRuntime &controller, std::size_t channel,
                      Json &buttons, const PsychicContacts &contacts) const
{
	for (const auto &[id, c] : definition(address.ship).psychic.choirs)
		for (const auto &b : c.seats)
		{
			const auto &owner = entities.at(address.ship).get<EntityControls>().controllers.at(b.controller);
			if (owner.definition.console != address.console)
				continue;
			const bool planning =
			    b.planner && &controller == &entities.at(address.ship).get<EntityControls>().controllers.at(*b.planner);
			const bool execution = &controller == &owner;
			if (!execution && !planning)
				continue;
			const bool enabled = channel == (planning ? b.planning_enabled : b.enabled);
			const bool target = channel == (planning ? b.planning_target : b.target);
			if (!enabled && !target)
				continue;
			const auto *o = choir_officer(address.ship, b.duty);
			std::vector<std::pair<EntityId, std::string>> choices;
			if (enabled)
				choices.push_back({controller.commands[channel].value ? 0ULL : 1ULL,
				                   controller.commands[channel].value ? "Disable power" : "Enable power"});
			else if (o && definition(address.ship).psychic.powers.at(o->power).kind != "cloak")
			{
				choices.push_back({0, "Clear psychic target"});
				if (definition(address.ship).psychic.powers.at(o->power).kind == "ward")
					choices.push_back({address.ship, "Protect own ship"});
				for (const auto &[contact_id, contact] : contacts)
					if (contact.id <= 1000000000ULL && psychic_target_valid(address.ship, b, contact.id, contacts))
						choices.push_back({contact.id, "Target"});
			}
			for (const auto &[value, label] : choices)
			{
				OperatingCommand cmd{address.ship,
				                     std::string(operating_action(controller.definition.commands[channel].action).name),
				                     static_cast<std::int64_t>(value),
				                     {},
				                     controller.definition.id,
				                     controller.definition.commands[channel].id,
				                     controller.generation};
				auto button = encode_operating_command(cmd);
				button["id"] = "psychic." + b.duty + "." + controller.definition.commands[channel].id + "." +
				               std::to_string(value);
				button["label"] = label;
				button["label_prefix"] =
				    (o ? o->name : b.duty) + (target && value ? " [" + std::to_string(value) + "]: " : ": ");
				buttons.push_back(button);
			}
			return true;
		}
	return false;
}
Json psychic_board(const ConsoleAddress &address) const
{
	Json out = {{"choirs", Json::array()}, {"fading", Json::array()}};
	for (const auto &[id, c] : definition(address.ship).psychic.choirs)
	{
		Json seats = Json::array();
		const auto &states = instance(entities.at(address.ship), id)->entity.get<PsychicChoirState>().seats;
		for (std::size_t i = 0; i < c.seats.size(); ++i)
		{
			const auto &b = c.seats[i];
			if (!personnel_owns(address, personnel_duty(address.ship, b.duty)->department))
				continue;
			const auto *o = choir_officer(address.ship, b.duty);
			const auto &s = states[i];
			const auto &controller = entities.at(address.ship).get<EntityControls>().controllers.at(b.controller);
			seats.push_back({{"duty", b.duty},
			                 {"officer", o ? o->name : ""},
			                 {"power", o ? definition(address.ship).psychic.powers.at(o->power).label : ""},
			                 {"controller", controller.definition.id},
			                 {"enabled_channel", controller.definition.commands[b.enabled].id},
			                 {"target_channel", controller.definition.commands[b.target].id},
			                 {"enabled", controller.commands[b.enabled].value != 0},
			                 {"target", controller.commands[b.target].value},
			                 {"work", s.work},
			                 {"ready", s.ready},
			                 {"blocker", !o            ? "No healthy psychic at duty"
			                             : !s.active   ? "Inactive or recovering"
			                             : s.work == 0 ? "Apparatus unavailable or unpowered"
			                                           : ""}});
		}
		if (!seats.empty())
			out["choirs"].push_back({{"id", id}, {"seats", seats}, {"emission", emission(address.ship)}});
	}
	for (const auto &[id, g] : ghost_receptions)
		if (g.observer == address.ship && g.ended >= 0)
		{
			bool permitted = false;
			for (const auto &sensor : seat(address.ship, address.console)->feed_systems)
				if (definition(address.ship).sensors.contains(sensor) &&
				    instance(entities.at(address.ship), sensor)->entity.get<SensorRuntime>().tracks.contains(g.id))
					permitted = true;
			if (!permitted)
				continue;
			const auto &p = definition(g.source).psychic.powers.at(g.power);
			const auto age = static_cast<std::int64_t>(ecs.get<Clock>().tick) - g.ended;
			if (age < p.fade_ticks)
				out["fading"].push_back(
				    {{"body", projection::body_json(ghost_body(g))}, {"opacity", 1000 - age * 1000 / p.fade_ticks}});
		}
	return out;
}
Json save_psychic() const
{
	Json out = {{"version", 1}, {"choirs", Json::array()}, {"receptions", Json::array()}, {"emissions", Json::array()}};
	for (auto child : ordered_equipment(choir_query))
		if (child.has<PsychicChoirState>())
			out["choirs"].push_back({{"ship", child.parent().get<StableId>().id},
			                         {"system", child.get<Equipment>().id},
			                         {"state", child.get<PsychicChoirState>()}});
	for (const auto &[id, g] : ghost_receptions)
		out["receptions"].push_back(g);
	for (const auto &[ship, until] : weapon_emissions)
		out["emissions"].push_back({{"ship", ship}, {"until", until}});
	return out;
}
void restore_psychic(const Json &saved)
{
	if (saved.empty())
		return;
	if (saved.value("version", 0) != 1 && (!saved.at("choirs").empty() || !saved.at("receptions").empty()))
		throw std::runtime_error("Psychic continuation changed; start a new session");
	if (!saved.is_object() || saved.at("choirs").size() > 2048 || saved.at("receptions").size() > 4096)
		throw std::runtime_error("Invalid psychic save bounds");
	const auto shape_valid = [](const PsychicGhostShape &g)
	{
		const auto in = [](std::int64_t v, std::int64_t lo, std::int64_t hi)
		{
			return v >= lo && v <= hi;
		};
		return in(g.pose.position.x, -1000000000, 1000000000) && in(g.pose.position.y, -1000000000, 1000000000) &&
		       in(g.motion.velocity.x, -1000000, 1000000) && in(g.motion.velocity.y, -1000000, 1000000) &&
		       in(g.pose.altitude, -1000000, 1000000) && in(g.pose.heading, 0, 359999) &&
		       in(g.motion.remainder.x, 1 - rate, rate - 1) && in(g.motion.remainder.y, 1 - rate, rate - 1) &&
		       in(g.acceleration_remainder, 0, rate - 1) && in(g.turn_remainder, 0, rate - 1);
	};
	std::set<std::pair<EntityId, std::string>> seen;
	for (const auto &row : saved.at("choirs"))
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !definition(ship).psychic.choirs.contains(id) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved choir");
		const auto &c = definition(ship).psychic.choirs.at(id);
		auto state = row.at("state").get<PsychicChoirState>();
		if (Json(state) != row.at("state") || state.seats.size() != c.seats.size())
			throw std::runtime_error("Invalid saved choir seats");
		for (auto &s : state.seats)
		{
			if (s.activation > ecs.get<Clock>().tick || s.ready > ecs.get<Clock>().tick + 36000 ||
			    s.target > 1000000000ULL || s.shapes.size() > 16 || (s.active && s.officer.empty()))
				throw std::runtime_error("Invalid psychic activation");
			const auto &officers = definition(ship).personnel.officers;
			const auto o = std::find_if(officers.begin(), officers.end(),
			                            [&](const auto &o)
			                            {
				                            return o.id == s.officer;
			                            });
			if (!s.officer.empty() && (o == officers.end() || !c.powers.contains(o->power)))
				throw std::runtime_error("Invalid psychic officer");
			if (s.aim_x < -fixed::unit || s.aim_x > fixed::unit || s.aim_y < -fixed::unit || s.aim_y > fixed::unit ||
			    (s.active && s.ready > ecs.get<Clock>().tick))
				throw std::runtime_error("Invalid psychic aim or recovery");
			if (s.active)
			{
				const auto &power = definition(ship).psychic.powers.at(o->power);
				if ((power.kind == "scramble" &&
				     (s.shapes.size() != static_cast<std::size_t>(power.ghosts) || !s.target || s.target == ship)) ||
				    (power.kind != "scramble" && !s.shapes.empty()))
					throw std::runtime_error("Contradictory psychic projection");
			}
			for (const auto &g : s.shapes)
				if (!shape_valid(g))
					throw std::runtime_error("Invalid apparent trajectory");
		}
		instance(entities.at(ship), id)->entity.set<PsychicChoirState>(state);
	}
	for (const auto &[ship, e] : entities)
		for (const auto &[id, c] : definition(ship).psychic.choirs)
			if (!seen.contains({ship, id}))
				throw std::runtime_error("Missing saved choir");
	refresh_psychic_work();
	refresh_psychic_reception();
	std::set<std::tuple<EntityId, EntityId, std::string, std::size_t, std::uint64_t, std::size_t>> reception_keys;
	for (const auto &row : saved.at("receptions"))
	{
		auto g = row.get<GhostReception>();
		if (Json(g) != row || !g.id || g.id >= next_entity || entities.contains(g.id) ||
		    !entities.contains(g.observer) || !entities.contains(g.source) ||
		    !definition(g.source).psychic.choirs.contains(g.choir) ||
		    g.seat >= definition(g.source).psychic.choirs.at(g.choir).seats.size() ||
		    !definition(g.source).psychic.powers.contains(g.power) ||
		    g.member >= static_cast<std::size_t>(definition(g.source).psychic.powers.at(g.power).ghosts) ||
		    !shape_valid(g.last) ||
		    !reception_keys.emplace(g.observer, g.source, g.choir, g.seat, g.activation, g.member).second ||
		    g.seen > ecs.get<Clock>().tick || g.activation > g.seen || g.ended < -1 ||
		    g.ended > static_cast<std::int64_t>(ecs.get<Clock>().tick) || (g.dispelled && g.ended < 0) ||
		    !ghost_receptions.emplace(g.id, g).second)
			throw std::runtime_error("Invalid saved ghost reception");
	}
	for (const auto &[id, g] : ghost_receptions)
		if (g.ended < 0)
		{
			const auto &state = instance(entities.at(g.source), g.choir)->entity.get<PsychicChoirState>().seats[g.seat];
			if (!state.active || !state.work || state.activation != g.activation || g.member >= state.shapes.size() ||
			    g.seen != ecs.get<Clock>().tick || Json(g.last) != Json(state.shapes[g.member]) ||
			    strongest_scrambler(g.observer) != std::tuple{g.source, g.choir, g.seat})
				throw std::runtime_error("Unearned live ghost reception");
		}
	for (const auto &row : saved.value("emissions", Json::array()))
	{
		const EntityId ship = row.at("ship");
		const std::uint64_t until = row.at("until");
		if (!entities.contains(ship) || until > ecs.get<Clock>().tick + 20 ||
		    !weapon_emissions.emplace(ship, until).second)
			throw std::runtime_error("Invalid saved emission");
	}
	// Funding is derived from the already-restored resource grants, without ticking or spending again.
	refresh_psychic_work();
	refresh_concealment();
}
