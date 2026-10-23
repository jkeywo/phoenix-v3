// Docking produces guidance; the owning flight Controller commits ordinary channels.
std::map<EntityId, fixed::Vec> dock_propulsion_starts;
std::vector<std::pair<EntityId, flecs::entity>> docking_rows() const
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	docking_query.each(
	    [&](flecs::entity e, const StableId &id, const DockingState &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}
std::vector<std::pair<EntityId, flecs::entity>> dock_connections() const
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	dock_connection_query.each(
	    [&](flecs::entity e, const StableId &id, const DockConnection &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}
bool dock_attached(EntityId id) const
{
	const auto *c = entities.at(id).try_get<DockConnection>();
	return c && c->phase != "approach";
}
EntityId dock_root(EntityId id) const
{
	std::set<EntityId> seen;
	while (dock_attached(id))
	{
		if (!seen.insert(id).second)
			throw std::runtime_error("Cyclic docking assembly");
		id = entities.at(id).get<DockConnection>().parent;
	}
	return id;
}
bool dock_busy(EntityId id) const
{
	if (entities.at(id).has<DockConnection>())
		return true;
	for (const auto &[other, e] : dock_connections())
		if (e.get<DockConnection>().parent == id)
			return true;
	return false;
}
bool dock_port_busy(EntityId id, std::size_t port) const
{
	for (const auto &[other, e] : dock_connections())
	{
		const auto &c = e.get<DockConnection>();
		if ((other == id && c.port == port) || (c.parent == id && c.remote_port == port))
			return true;
	}
	return false;
}
fixed::Vec dock_rotate(fixed::Vec v, std::int64_t heading) const
{
	const auto d = fixed::direction(heading);
	return {v.x * d.x / fixed::unit - v.y * d.y / fixed::unit, v.x * d.y / fixed::unit + v.y * d.x / fixed::unit};
}
fixed::Vec dock_position(EntityId id, std::size_t port) const
{
	const auto &p = definition(id).docking.at(port);
	const auto &pose = entities.at(id).get<Pose>();
	return pose.position + dock_rotate({p.x, p.y}, pose.heading);
}
bool dock_port_working(EntityId id, std::size_t port) const
{
	if (!entities.contains(id) || combat_dead(entities.at(id)) || departed(id) || craft_dormant(entities.at(id)))
		return false;
	const auto &p = definition(id).docking.at(port);
	const auto compartment = definition(id).combat->compartment_ids.at(p.compartment);
	return !entities.at(id).get<CombatStructure>().compartments.at(compartment).disabled;
}
bool dock_permitted(EntityId ship, std::size_t port, EntityId target, std::size_t remote) const
{
	const auto &a = definition(ship).docking.at(port), &b = definition(target).docking.at(remote);
	if (a.compatibility != b.compatibility || !dock_port_working(ship, port) || !dock_port_working(target, remote))
		return false;
	const auto &s = entities.at(target).get<DockingState>().ports.at(remote);
	if (s.permitted.contains(ship))
		return true;
	if (s.accepting && s.supply)
		for (const auto &job : cargo_deliveries)
			if (job.collection && !job.cancelled && job.carrier == ship && job.target == target &&
			    job.phase != "complete" && job.phase != "lost")
				return true;
	// A target-authored boarding port can be forced only once the ordinary
	// boarding insertion condition is met and the crew has opened that mission.
	for (std::size_t i = 0; i < away_definitions.size(); ++i)
	{
		const auto &mission = away_definitions[i];
		const auto phase = away_missions[i].phase;
		if (mission.boarding && mission.allow_ship_transport && mission.target == target &&
		    away_ship(mission) == ship &&
		    (phase == AwayPhase::Open || phase == AwayPhase::Recovered || phase == AwayPhase::Outbound ||
		     phase == AwayPhase::Deployed || phase == AwayPhase::Inbound) &&
		    std::find(mission.docking_ports.begin(), mission.docking_ports.end(), b.id) !=
		        mission.docking_ports.end() &&
		    boarding_insertion_blocker(i, "ship").empty())
			return true;
	}
	const auto *x = entities.at(ship).try_get<FactionMembership>(),
	           *y = entities.at(target).try_get<FactionMembership>();
	return s.accepting && x && y && factions.relationship(x->id, y->id) == "friendly";
}
bool dock_known(const ConsoleAddress &a, EntityId target) const
{
	for (const auto &contact : capture_sensor_contacts(a))
		if (contact.id == target)
			return true;
	for (std::size_t i = 0; i < away_definitions.size(); ++i)
		if (away_ship(away_definitions[i]) == a.ship && away_definitions[i].target == target &&
		    away_missions[i].phase != AwayPhase::Hidden)
			return true;
	if (const auto *c = entities.at(a.ship).try_get<CraftState>(); c && (c->home == target || c->target == target))
		return true;
	return false;
}
bool dock_envelope(EntityId id, const DockConnection &c) const
{
	const auto &a = definition(id).docking.at(c.port), &b = definition(c.parent).docking.at(c.remote_port);
	const auto &own = entities.at(id).get<Pose>(), &other = entities.at(c.parent).get<Pose>();
	const auto z = altitude(id) + a.z - altitude(c.parent) - b.z;
	const auto delta = dock_position(id, c.port) - dock_position(c.parent, c.remote_port);
	const auto speed = entities.at(id).get<Motion>().velocity - entities.at(c.parent).get<Motion>().velocity;
	const auto vz =
	    entities.at(id).get<Motion>().vertical_velocity - entities.at(c.parent).get<Motion>().vertical_velocity;
	const auto alignment = std::abs((own.heading + a.heading - other.heading - b.heading + 360000) % 360000 - 180000);
	const auto range = std::min(a.capture_range, b.capture_range), limit = std::min(a.capture_speed, b.capture_speed);
	return fixed::dot(delta, delta) + z * z <= range * range && fixed::dot(speed, speed) + vz * vz <= limit * limit &&
	       alignment <= std::min(a.alignment, b.alignment);
}
bool dock_ready(EntityId a, EntityId b) const
{
	const auto *c = entities.at(a).try_get<DockConnection>();
	return c && c->parent == b && c->phase == "docked" && dock_port_working(a, c->port) &&
	       dock_port_working(b, c->remote_port);
}
bool away_dock_ready(EntityId ship, const AwayDefinition &mission, bool finishing = false) const
{
	const auto *connection = entities.at(ship).try_get<DockConnection>();
	if (!connection || connection->parent != mission.target ||
	    (!dock_ready(ship, mission.target) &&
	     !(finishing && connection->phase == "separating" && dock_port_working(ship, connection->port) &&
	       dock_port_working(mission.target, connection->remote_port))))
		return false;
	const auto &c = entities.at(ship).get<DockConnection>();
	const auto &port = definition(mission.target).docking.at(c.remote_port);
	return std::find(mission.docking_ports.begin(), mission.docking_ports.end(), port.id) !=
	       mission.docking_ports.end();
}
void detach_dock(EntityId id, bool emergency)
{
	const auto e = entities.at(id);
	const auto c = e.get<DockConnection>();
	const auto &p = definition(id).docking.at(c.port), &q = definition(c.parent).docking.at(c.remote_port);
	if (emergency && p.emergency_damage)
		combat_damage(e, definition(id).combat->compartment_ids.at(p.compartment), p.emergency_damage);
	if (emergency && q.emergency_damage)
		combat_damage(entities.at(c.parent), definition(c.parent).combat->compartment_ids.at(q.compartment),
		              q.emergency_damage);
	e.remove<DockConnection>();
	e.remove<CraftGuidance>();
	++e.get_mut<DockingState>().revision;
	++entities.at(c.parent).get_mut<DockingState>().revision;
}
void cancel_docking_assist(EntityId id, const std::string &controller, const std::string &action, std::int64_t value)
{
	const auto e = entities.at(id);
	auto *c = e.try_get_mut<DockConnection>();
	if ((action == "longitudinal_thrust" || action == "lateral_thrust" || action == "manual_steering") && value == 0)
		return;
	if (c && c->phase == "approach" && c->generations.contains(controller) &&
	    (action == "speed_mm_s" || action == "heading_mdeg" || action == "longitudinal_thrust" ||
	     action == "lateral_thrust" || action == "manual_steering" || action == "assisted" || action == "destination" ||
	     action == "follow_contact" || action == "boost" || action == "impulse" || action == "match_speed" ||
	     action == "altitude_mm"))
		detach_dock(id, false);
}
bool docking_command(const ConsoleAddress &a, const OperatingCommand &cmd, bool execute, bool ai = false)
{
	const auto e = entities.at(a.ship);
	const auto &ports = definition(a.ship).docking;
	if (!e.has<DockingState>() || cmd.channel != "" || cmd.request_id || !cmd.arguments.empty() ||
	    cmd.text.size() > 256 || !cmd.speaker_name.empty() || !cmd.speaker_avatar.empty())
		return false;
	auto &state = e.get_mut<DockingState>();
	if (cmd.value != static_cast<std::int64_t>(state.revision) || state.revision >= 9007199254740991ULL)
		return false;
	std::size_t port = ports.size();
	for (std::size_t i = 0; i < ports.size(); ++i)
		if (ports[i].id == cmd.system)
			port = i;
	if (port == ports.size() || !material_authority(a, cmd, ports[port].controller, ai))
		return false;
	if (cmd.action == "dock.accept" || cmd.action == "dock.reject" || cmd.action == "dock.supply")
	{
		const auto n = parse_material_number(cmd.text);
		if (n < 0 || (cmd.action == "dock.supply" && n > 1))
			return false;
		if (n && cmd.action != "dock.supply" && (!entities.contains(n) || !dock_known(a, n)))
			return false;
		if (execute)
		{
			auto &s = state.ports[port];
			if (cmd.action == "dock.supply")
				s.supply = n;
			else if (!n)
				s.accepting = cmd.action == "dock.accept";
			else if (cmd.action == "dock.accept")
				s.permitted.insert(n);
			else
				s.permitted.erase(n);
			++state.revision;
		}
		return true;
	}
	if (cmd.action == "dock.undock" || cmd.action == "dock.emergency" || cmd.action == "dock.cancel")
	{
		if (!cmd.text.empty())
			return false;
		EntityId follower = 0;
		for (const auto &[id, other] : dock_connections())
		{
			const auto &c = other.get<DockConnection>();
			if ((id == a.ship && c.port == port) || (c.parent == a.ship && c.remote_port == port))
				follower = id;
		}
		if (!follower)
			return false;
		auto &c = entities.at(follower).get_mut<DockConnection>();
		if (cmd.action == "dock.emergency" && !ports[port].emergency_release)
			return false;
		if (cmd.action == "dock.cancel" && c.phase != "approach")
			return false;
		if (execute)
		{
			if (cmd.action != "dock.undock" || c.phase == "approach")
				detach_dock(follower, cmd.action == "dock.emergency");
			else
			{
				c.phase = "separating";
				c.progress = 0;
				c.assisted = false;
				++state.revision;
			}
		}
		return true;
	}
	if (cmd.action != "dock.connect" && cmd.action != "dock.assist")
		return false;
	const auto slash = cmd.text.find('/');
	if (slash == std::string::npos)
		return false;
	const auto number = parse_material_number(cmd.text.substr(0, slash));
	if (number < 1)
		return false;
	const auto target = static_cast<EntityId>(number);
	if (target == a.ship || !entities.contains(target) || !entities.at(target).has<DockingState>() ||
	    !dock_known(a, target) || entities.at(a.ship).has<DockConnection>() || dock_port_busy(a.ship, port) ||
	    dock_chain_contains(target, a.ship))
		return false;
	std::size_t remote = definition(target).docking.size();
	for (std::size_t i = 0; i < definition(target).docking.size(); ++i)
		if (definition(target).docking[i].id == cmd.text.substr(slash + 1))
			remote = i;
	if (remote == definition(target).docking.size() || dock_port_busy(target, remote) ||
	    !dock_permitted(a.ship, port, target, remote) || ftl_preparing(a.ship) || ftl_preparing(target))
		return false;
	if (const auto *craft = e.try_get<CraftState>())
	{
		const bool supply = ai && std::any_of(cargo_deliveries.begin(), cargo_deliveries.end(),
		                                      [&](const auto &job)
		                                      {
			                                      return job.collection && !job.cancelled && job.carrier == a.ship &&
			                                             job.target == target && job.mission == craft->mission &&
			                                             job.phase == "outbound";
		                                      });
		if (craft->phase != CraftPhase::Flying || (!craft->mission.empty() && !supply))
			return false;
	}
	DockConnection c;
	c.parent = target;
	c.port = port;
	c.remote_port = remote;
	c.controller = cmd.controller;
	c.generation = cmd.generation;
	c.assisted = cmd.action == "dock.assist";
	if (c.assisted)
	{
		bool speed = false, heading = false;
		for (const auto &ctrl : e.get<EntityControls>().controllers)
			if (ctrl.definition.console == a.console)
			{
				c.generations[ctrl.definition.id] = ctrl.generation;
				for (const auto &ch : ctrl.definition.commands)
				{
					speed |= ch.action == OperatingAction::Speed;
					heading |= ch.action == OperatingAction::Heading;
				}
			}
		if (!speed || !heading)
			return false;
	}
	if (execute)
	{
		e.set<DockConnection>(c);
		++state.revision;
		++entities.at(target).get_mut<DockingState>().revision;
	}
	return true;
}
void prepare_docking()
{
	for (const auto &[id, e] : dock_connections())
	{
		auto &c = e.get_mut<DockConnection>();
		if (c.phase != "approach" || !c.assisted)
			continue;
		const auto &own = e.get<Pose>();
		std::string console;
		for (const auto &ctrl : e.get<EntityControls>().controllers)
			if (ctrl.definition.id == c.controller)
				console = ctrl.definition.console;
		if (!dock_report_available({id, console}, c.parent))
		{
			CraftGuidance g;
			g.heading = own.heading;
			e.set<CraftGuidance>(g);
			continue;
		}
		const auto &other = entities.at(c.parent).get<Pose>();
		const auto &a = definition(id).docking.at(c.port), &b = definition(c.parent).docking.at(c.remote_port);
		CraftGuidance g;
		g.target = c.parent;
		g.navigate = true;
		const auto desired_heading = (other.heading + b.heading + 180000 - a.heading) % 360000;
		g.destination = dock_position(c.parent, c.remote_port) - dock_rotate({a.x, a.y}, desired_heading);
		g.altitude = altitude(c.parent) + b.z - a.z;
		bool detour = false;
		// A port on the far side of a hull needs an approach around that hull.
		// Test the actual shapes, with a small navigation clearance, before choosing a side waypoint.
		auto approach_pose = own;
		approach_pose.position = g.destination;
		approach_pose.heading = desired_heading;
		auto shape = e.get<Collider>();
		if (shape.box)
		{
			shape.half_width += 1000;
			shape.half_length += 1000;
		}
		else
			shape.radius += 1000;
		const CollisionBody moving{approach_pose, Motion{}, shape},
		    obstacle{other, Motion{}, entities.at(c.parent).get<Collider>()};
		if (sweep_contact_time(moving, obstacle, own.position, other.position, false))
		{
			const auto outward = fixed::direction((other.heading + b.heading) % 360000);
			const auto side = fixed::direction((other.heading + b.heading + 90000) % 360000);
			const auto offset = own.position - other.position;
			const auto sign = fixed::dot(offset, side) >= 0 ? 1 : -1;
			const auto &target_shape = entities.at(c.parent).get<Collider>();
			const auto radius =
			    (target_shape.box ? target_shape.half_width + target_shape.half_length : target_shape.radius) +
			    (shape.box ? shape.half_width + shape.half_length : shape.radius) + 3000;
			// Clear the side of the hull before advancing around its far end.
			// Holding the starting longitudinal offset forever cannot reach a far-side port.
			const auto side_distance = std::abs(fixed::dot(offset, side) / fixed::unit);
			const auto along = side_distance < radius * 3 / 4
			                       ? std::min<std::int64_t>(0, fixed::dot(offset, outward) / fixed::unit)
			                       : radius;
			g.destination = other.position + fixed::scale(side, sign * radius) + fixed::scale(outward, along);
			detour = true;
		}
		const auto delta = g.destination - own.position;
		const auto distance = fixed::length(delta);
		std::int64_t maximum = 10000, acceleration = 1000;
		bool inertial_engine = false, lateral_thrust = false;
		for (auto child : e.get<EquipmentIndex>().values)
		{
			if (const auto *f = child.try_get<FlightRuntime>())
				lateral_thrust |= f->definition.lateral_acceleration > 0;
			if (const auto *f = child.try_get<FlightRuntime>();
			    f && child.get<Equipment>().kind == OperatingSystem::Engine)
			{
				inertial_engine = true;
				maximum = f->definition.maximum_speed;
				acceleration = f->definition.acceleration;
			}
			else if (const auto *engine = child.try_get<Engine>())
			{
				maximum = engine->max_speed;
				acceleration = engine->acceleration;
			}
		}
		const auto parent_velocity = entities.at(c.parent).get<Motion>().velocity;
		const auto relative = e.get<Motion>().velocity - parent_velocity;
		const auto approach_speed =
		    std::min({maximum / 4, distance / 8,
		              static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(2 * acceleration * distance)))});
		const auto desired_velocity = parent_velocity + fixed::scale(fixed::normal(delta), approach_speed);
		const auto guidance = pursuit_guidance(own, e.get<Motion>(), desired_velocity, maximum, desired_heading);
		g.heading = guidance.heading;
		g.speed = guidance.speed;
		if (inertial_engine && !lateral_thrust)
		{
			const auto bearing = [&](fixed::Vec direction)
			{
				return pursuit_guidance(own, Motion{}, direction, maximum, desired_heading).heading;
			};
			const auto angle = [](std::int64_t a, std::int64_t b)
			{
				return std::abs((a - b + 540000) % 360000 - 180000);
			};
			const auto tolerance = std::max<std::int64_t>(1, std::min(a.capture_speed, b.capture_speed) / 4);
			const auto remaining = fixed::length(relative);
			const auto course = bearing(delta);
			const bool at_port = !detour && distance < std::min(a.capture_range, b.capture_range) / 2;
			if (c.guidance_step == 0)
			{
				c.guidance_heading = remaining > tolerance ? bearing(relative) : at_port ? desired_heading : course;
				c.guidance_step = remaining > tolerance ? 1 : at_port ? 5 : 3;
			}
			g.heading = c.guidance_heading;
			const auto facing = fixed::direction(own.heading);
			// Turning with neutral longitudinal acceleration preserves the velocity we intend to brake.
			g.speed = std::clamp(fixed::dot(e.get<Motion>().velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
			if (c.guidance_step == 1 && angle(own.heading, c.guidance_heading) < 1000)
				c.guidance_step = 2;
			if (c.guidance_step == 2)
			{
				g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
				if (remaining <= tolerance || std::abs(fixed::dot(relative, facing) / fixed::unit) <= tolerance)
					c.guidance_step = 0; // Re-latch any small residual lateral drift before another turn.
			}
			else if (c.guidance_step == 3 && angle(own.heading, c.guidance_heading) < 1000)
				c.guidance_step = 4;
			if (c.guidance_step == 4)
			{
				if (at_port || angle(course, c.guidance_heading) > 5000)
					c.guidance_step = 0;
				else
					g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit + approach_speed,
					                     std::int64_t{0}, maximum);
			}
			else if (c.guidance_step == 5)
			{
				g.heading = desired_heading;
				g.speed = std::clamp(fixed::dot(parent_velocity, facing) / fixed::unit, std::int64_t{0}, maximum);
				if (!at_port || remaining > tolerance)
					c.guidance_step = 0;
			}
		}
		if (!(inertial_engine && !lateral_thrust) && !detour && distance < std::min(a.capture_range, b.capture_range) &&
		    fixed::length(relative) <= std::min(a.capture_speed, b.capture_speed) / 2)
		{
			g.heading = desired_heading;
			g.speed =
			    std::max<std::int64_t>(0, fixed::dot(parent_velocity, fixed::direction(desired_heading)) / fixed::unit);
		}
		e.set<CraftGuidance>(g);
	}
}
void apply_docking_controller(flecs::entity e)
{
	const auto *connection = e.try_get<DockConnection>();
	if (!connection || connection->phase != "approach" || !connection->assisted)
		return;
	const auto c = *connection;
	const auto id = e.get<StableId>().id;
	auto &controllers = e.get_mut<EntityControls>().controllers;
	for (std::size_t index = 0; index < controllers.size(); ++index)
	{
		auto &ctrl = controllers[index];
		if (!c.generations.contains(ctrl.definition.id))
			continue;
		if (ctrl.generation != c.generations.at(ctrl.definition.id))
		{
			detach_dock(id, false);
			return;
		}
		const auto &g = e.get<CraftGuidance>();
		for (std::size_t i = 0; i < ctrl.definition.commands.size(); ++i)
		{
			const auto &ch = ctrl.definition.commands[i];
			std::optional<std::int64_t> value;
			if (ch.action == OperatingAction::Speed)
				value = g.speed;
			if (ch.action == OperatingAction::Heading)
				value = g.heading;
			if (ch.action == OperatingAction::Assisted)
				value = 1;
			if (ch.action == OperatingAction::Altitude)
				value = g.altitude;
			if (value)
				commit_controller({{id, ctrl.definition.console, ctrl.definition.id},
				                   index,
				                   i,
				                   *value,
				                   ctrl.generation,
				                   0,
				                   ctrl.human,
				                   {}});
		}
	}
}
bool dock_crossing_busy(EntityId a, EntityId b) const
{
	for (const auto &batch : material_batches)
		if (batch.source != batch.destination && batch.crossing &&
		    ((batch.source == a && batch.destination == b) || (batch.source == b && batch.destination == a)))
			return true;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		const auto &m = away_missions[i];
		const auto &d = away_definitions[i];
		if (m.transport == "ship" && m.transfer > 0 &&
		    ((away_ship(d) == a && d.target == b) || (away_ship(d) == b && d.target == a)) &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound))
			return true;
	}
	return false;
}
void step_docking()
{
	for (const auto &[id, e] : dock_connections())
	{
		auto &c = e.get_mut<DockConnection>();
		if (combat_dead(e) || combat_dead(entities.at(c.parent)) || departed(c.parent))
		{
			detach_dock(id, false);
			continue;
		}
		const auto &a = definition(id).docking.at(c.port), &b = definition(c.parent).docking.at(c.remote_port);
		if (c.phase == "approach")
		{
			if (!dock_permitted(id, c.port, c.parent, c.remote_port))
			{
				detach_dock(id, false);
				continue;
			}
			if (!dock_envelope(id, c))
				continue;
			const auto &parent = entities.at(c.parent).get<Pose>();
			const auto heading = (parent.heading + b.heading + 180000 - a.heading) % 360000;
			c.relative_heading = (heading - parent.heading + 360000) % 360000;
			c.relative = {b.x, b.y};
			c.relative = c.relative - dock_rotate({a.x, a.y}, c.relative_heading);
			c.relative_z = b.z - a.z;
			c.phase = "mating";
			c.progress = 0;
			c.assisted = false;
			// Capture momentum once; a static receiving facility anchors the assembly.
			const auto root = dock_root(c.parent);
			const auto mass = dock_mass(root);
			const auto own_mass = definition(id).collider.mass;
			if (mass > 0 && own_mass > 0)
			{
				const auto v = entities.at(root).get<Motion>().velocity, own = e.get<Motion>().velocity;
				entities.at(root).get_mut<Motion>().velocity = {v.x + (own.x - v.x) * own_mass / mass,
				                                                v.y + (own.y - v.y) * own_mass / mass};
			}
			++e.get_mut<DockingState>().revision;
			e.remove<CraftGuidance>();
		}
		else if (c.phase == "mating")
		{
			if (dock_port_working(id, c.port) && dock_port_working(c.parent, c.remote_port) &&
			    ++c.progress >= std::max(a.mating_ticks, b.mating_ticks))
			{
				c.phase = "docked";
				c.progress = 0;
				++e.get_mut<DockingState>().revision;
				++entities.at(c.parent).get_mut<DockingState>().revision;
			}
		}
		else if (c.phase == "separating" && !dock_crossing_busy(id, c.parent))
		{
			if (++c.progress >= std::max(a.separation_ticks, b.separation_ticks))
			{
				detach_dock(id, false);
				continue;
			}
		}
	}
	sync_docks();
}
void sync_docks()
{
	const auto rows = dock_connections();
	// Parents may have greater identities; topological passes are bounded by port count.
	for (std::size_t pass = 0; pass < rows.size(); ++pass)
		for (const auto &[id, e] : rows)
		{
			const auto &c = e.get<DockConnection>();
			if (c.phase == "approach")
				continue;
			const auto parent = entities.at(c.parent);
			const auto &pose = parent.get<Pose>();
			auto &p = e.get_mut<Pose>();
			p.position = pose.position + dock_rotate(c.relative, pose.heading);
			p.heading = (pose.heading + c.relative_heading) % 360000;
			p.altitude = altitude(c.parent) + c.relative_z;
			e.get_mut<Motion>().remainder = parent.get<Motion>().remainder;
			e.get_mut<Motion>().velocity = parent.get<Motion>().velocity;
			e.get_mut<Motion>().vertical_velocity = parent.get<Motion>().vertical_velocity;
			if (auto *v = e.try_get_mut<VerticalMotion>())
			{
				v->altitude = p.altitude;
				v->velocity = parent.get<Motion>().vertical_velocity;
			}
		}
}
std::int64_t dock_mass(EntityId root) const
{
	if (!definition(root).collider.mass)
		return 0;
	std::int64_t mass = definition(root).collider.mass;
	for (const auto &[id, e] : dock_connections())
		if (id != root && dock_root(id) == root)
			mass += definition(id).collider.mass;
	return mass;
}
void capture_dock_propulsion()
{
	dock_propulsion_starts.clear();
	for (const auto &[id, e] : dock_connections())
	{
		const auto root = dock_root(id);
		if (root != id)
			dock_propulsion_starts.try_emplace(root, entities.at(root).get<Motion>().velocity);
	}
}
void adjust_dock_propulsion()
{
	for (const auto &[id, before] : dock_propulsion_starts)
	{
		const auto mass = dock_mass(id), bare = definition(id).collider.mass;
		if (!mass || !bare)
			continue;
		auto &v = entities.at(id).get_mut<Motion>().velocity;
		v = {before.x + (v.x - before.x) * bare / mass, before.y + (v.y - before.y) * bare / mass};
	}
}
void dock_collision_response(std::size_t index, const CollisionBody &before)
{
	const auto &after = collisions[index];
	const auto root = dock_root(after.id);
	const auto position = after.body.position - before.position, velocity = after.body.velocity - before.velocity;
	for (std::size_t i = 0; i < collisions.size(); ++i)
		if (i != index && dock_root(collisions[i].id) == root)
		{
			collisions[i].body.position = collisions[i].body.position + position;
			collisions[i].body.velocity = collisions[i].body.velocity + velocity;
		}
}
Json docking_board(const ConsoleAddress &a) const
{
	Json out{{"revision", 1}, {"ports", Json::array()}, {"targets", Json::array()}};
	const auto e = entities.at(a.ship);
	if (!e.has<DockingState>())
		return out;
	const auto &state = e.get<DockingState>();
	out["revision"] = state.revision;
	for (std::size_t i = 0; i < definition(a.ship).docking.size(); ++i)
	{
		const auto &p = definition(a.ship).docking[i];
		bool owned = false;
		for (const auto &ctrl : e.get<EntityControls>().controllers)
			owned |= ctrl.definition.console == a.console && ctrl.definition.id == p.controller;
		if (!owned)
			continue;
		Json row{{"id", p.id},
		         {"controller", p.controller},
		         {"compatibility", p.compatibility},
		         {"accepting", state.ports[i].accepting},
		         {"supply", state.ports[i].supply},
		         {"working", dock_port_working(a.ship, i)},
		         {"emergency", p.emergency_release},
		         {"phase", "free"},
		         {"target", 0},
		         {"progress", 0}};
		for (const auto &[id, other] : dock_connections())
		{
			const auto &c = other.get<DockConnection>();
			if ((id == a.ship && c.port == i) || (c.parent == a.ship && c.remote_port == i))
			{
				row["phase"] = c.phase;
				row["target"] = id == a.ship ? c.parent : id;
				row["progress"] = c.progress;
			}
		}
		out["ports"].push_back(row);
	}
	if (out["ports"].empty())
		return out;
	for (const auto &[id, other] : docking_rows())
		if (id != a.ship && !craft_dormant(other) && dock_known(a, id))
			for (std::size_t i = 0; i < definition(id).docking.size(); ++i)
			{
				const auto &p = definition(id).docking[i];
				out["targets"].push_back({{"entity", id},
				                          {"port", p.id},
				                          {"compatibility", p.compatibility},
				                          {"available", !dock_port_busy(id, i)}});
				if (out["targets"].size() >= 128)
					return out;
			}
	return out;
}
void restore_docking(const Json &saved)
{
	for (const auto &b : saved.at("bodies"))
	{
		const auto id = b.at("body").at("id").get<EntityId>();
		const auto e = entities.at(id);
		if (e.has<DockingState>())
		{
			const auto s = b.at("docking").get<DockingState>();
			if (!s.revision || s.revision >= 9007199254740991ULL || s.ports.size() != definition(id).docking.size())
				throw std::runtime_error("Invalid docking port continuation");
			for (const auto &p : s.ports)
			{
				if (p.permitted.size() > 128)
					throw std::runtime_error("Excessive docking permissions");
				for (const auto target : p.permitted)
					if (!entities.contains(target) || target == id)
						throw std::runtime_error("Invalid docking permission");
			}
			e.set<DockingState>(s);
		}
		else if (b.contains("docking") || b.contains("dock_connection"))
			throw std::runtime_error("Unexpected saved docking equipment");
		if (b.contains("dock_connection"))
		{
			auto saved_connection = b.at("dock_connection");
			if (!saved_connection.contains("guidance_step"))
				saved_connection["guidance_step"] = 0;
			if (!saved_connection.contains("guidance_heading"))
				saved_connection["guidance_heading"] = 0;
			const auto c = saved_connection.get<DockConnection>();
			if (c.parent == id || !entities.contains(c.parent) || c.port >= definition(id).docking.size() ||
			    c.remote_port >= definition(c.parent).docking.size() || c.progress < 0 || c.progress > 36000 ||
			    (c.phase != "approach" && c.phase != "mating" && c.phase != "docked" && c.phase != "separating") ||
			    c.relative_heading < 0 || c.relative_heading >= 360000 || std::abs(c.relative.x) > 20000000 ||
			    std::abs(c.relative.y) > 20000000 || std::abs(c.relative_z) > 2000000 || c.guidance_step < 0 ||
			    c.guidance_step > 5 || c.guidance_heading < 0 || c.guidance_heading >= 360000)
				throw std::runtime_error("Invalid docking connection continuation");
			if (definition(id).docking[c.port].compatibility !=
			    definition(c.parent).docking[c.remote_port].compatibility)
				throw std::runtime_error("Incompatible saved docking ports");
			e.set<DockConnection>(c);
		}
	}
	std::set<std::pair<EntityId, std::size_t>> used;
	for (const auto &[id, e] : dock_connections())
	{
		const auto &c = e.get<DockConnection>();
		(void)dock_root(id);
		if (dock_chain_contains(c.parent, id))
			throw std::runtime_error("Cyclic saved docking request");
		if (!used.emplace(id, c.port).second || !used.emplace(c.parent, c.remote_port).second)
			throw std::runtime_error("Double-booked docking port");
		if (c.phase != "approach")
		{
			const auto &a = definition(id).docking[c.port], &b = definition(c.parent).docking[c.remote_port];
			const auto heading = (b.heading + 180000 - a.heading + 360000) % 360000;
			const auto relative = fixed::Vec{b.x, b.y} - dock_rotate({a.x, a.y}, heading);
			if (c.relative_heading != heading || c.relative.x != relative.x || c.relative.y != relative.y ||
			    c.relative_z != b.z - a.z)
				throw std::runtime_error("Invalid saved docking transform");
			const auto &parent = entities.at(c.parent).get<Pose>();
			const auto &own = e.get<Pose>();
			const auto position = parent.position + dock_rotate(relative, parent.heading);
			if (own.position.x != position.x || own.position.y != position.y ||
			    own.heading != (parent.heading + heading) % 360000 || own.altitude != parent.altitude + c.relative_z)
				throw std::runtime_error("Saved docking body does not match its connection");
		}
	}
}

std::optional<MaterialGateway> material_gateway(EntityId source, EntityId destination, bool finishing = false) const
{
	auto docking = [&](EntityId child, EntityId parent, bool reverse) -> std::optional<MaterialGateway>
	{
		const auto *c = entities.at(child).try_get<DockConnection>();
		if (!c || c->parent != parent || (c->phase != "docked" && !(finishing && c->phase == "separating")) ||
		    !dock_port_working(child, c->port) || !dock_port_working(parent, c->remote_port))
			return {};
		if (!entities.at(child).get<DockingState>().ports[c->port].supply ||
		    !entities.at(parent).get<DockingState>().ports[c->remote_port].supply)
			return {};
		const auto &a = definition(child).docking[c->port], &b = definition(parent).docking[c->remote_port];
		const auto ca = definition(child).combat->compartment_ids.at(a.compartment),
		           cb = definition(parent).combat->compartment_ids.at(b.compartment);
		return MaterialGateway{reverse ? cb : ca, reverse ? ca : cb, std::min(a.throughput, b.throughput)};
	};
	if (auto g = docking(source, destination, false))
		return g;
	if (auto g = docking(destination, source, true))
		return g;
	auto bay = [&](EntityId craft, EntityId home, bool reverse) -> std::optional<MaterialGateway>
	{
		const auto *s = entities.at(craft).try_get<CraftState>();
		if (!s || s->home != home || s->phase != CraftPhase::Docked || !bay_working(home, s->bay))
			return {};
		const auto &b = definition(home).craft_bays.at(s->bay);
		const auto from = definition(home).combat->compartment_ids.at(b.compartment);
		const auto &p = definition(craft).transport.compartment;
		const auto to = p.empty() ? 0 : definition(craft).combat->compartment_ids.at(p);
		return MaterialGateway{reverse ? to : from, reverse ? from : to,
		                       std::min(definition(home).logistics.throughput, definition(craft).logistics.throughput)};
	};
	if (auto g = bay(destination, source, false))
		return g;
	return bay(source, destination, true);
}

bool dock_chain_contains(EntityId id, EntityId needle) const
{
	std::set<EntityId> seen;
	while (true)
	{
		if (id == needle)
			return true;
		if (!seen.insert(id).second)
			throw std::runtime_error("Cyclic docking request");
		const auto *c = entities.at(id).try_get<DockConnection>();
		if (!c)
			return false;
		id = c->parent;
	}
}
bool dock_report_available(const ConsoleAddress &a, EntityId target) const
{
	for (const auto &contact : capture_sensor_contacts(a))
		if (contact.id == target)
			return true;
	const auto x = conversations.endpoints.find(a.ship), y = conversations.endpoints.find(target);
	return x != conversations.endpoints.end() && y != conversations.endpoints.end() &&
	       comms_endpoints_linked(x->second, y->second);
}
