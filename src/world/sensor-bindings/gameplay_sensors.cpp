module;
#include "native_headers.hpp"

module phoenix.simulation;
import :sensor_bindings;

namespace phoenix::world_detail
{
auto Runtime::sensor_definition(EntityId ship, const std::string &id) const -> const SensorDefinition &
{
	return definition(ship).sensors.at(id);
}

auto Runtime::sensor_acquisition_definition(EntityId ship, const std::string &id) const -> SensorDefinition
{
	auto result = sensor_definition(ship, id);
	if (const auto *mode = directed_mode(ship, id); mode && mode->operation == DirectedOperationKind::Scan)
		result.capabilities = mode->capabilities;
	return result;
}

auto Runtime::directed_scan_covers(EntityId ship, const std::string &id, const BodyView &body) const -> bool
{
	const auto *mode = directed_mode(ship, id);
	if (!mode || mode->operation != DirectedOperationKind::Scan)
		return false;
	const auto &d = sensor_definition(ship, id);
	const auto origin = sensor_origin(ship, d);
	const auto range = effective_property(entities.at(ship), id, PropertyKey::sensors, mode->range);
	const auto delta = fixed::Vec{body.x, body.y} - origin;
	const auto z = body.altitude - altitude(ship);
	return fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(delta, delta) + z * z)) <= range &&
	       scenario_sensor_visible(regions, {origin.x, origin.y}, {body.x, body.y}, range) &&
	       (d.arc == 360000 || std::abs(combat_delta(combat_bearing(delta), sensor_heading(ship, d))) <= d.arc / 2);
}

auto Runtime::sensor_heading(EntityId ship, const SensorDefinition &d) const -> std::int64_t
{
	auto heading = entities.at(ship).get<Pose>().heading + d.bearing;
	if (!d.turret.empty())
		heading += instance(entities.at(ship), d.turret)->entity.get<CombatMechanism>().get<TurretState>().heading;
	return combat_angle(heading);
}

auto Runtime::sensor_origin(EntityId ship, const SensorDefinition &d) const -> fixed::Vec
{
	const auto entity = entities.at(ship);
	if (d.turret.empty())
		return entity.get<Pose>().position;
	const auto &m = instance(entity, d.turret)->entity.get<CombatMechanism>().definition;
	return combat_world({m.x, m.y}, entity.get<Pose>());
}

auto Runtime::sensor_coverage(EntityId ship, const std::string &id, bool include_outer) const -> SensorCoverage
{
	const auto &d = sensor_definition(ship, id);
	SensorCoverage result;
	result.origin = sensor_origin(ship, d);
	result.normal = sensor_range(ship, id);
	result.outer = include_outer ? sensor_range(ship, id, true) : 0;
	result.arc = d.arc;
	if (d.arc != 360000)
		result.heading = sensor_heading(ship, d);
	if (!d.turret.empty())
	{
		const auto source = entities.at(ship);
		result.mount_enabled = instance(source, d.turret)->enabled && !structurally_disabled(source, d.turret);
	}
	return result;
}

auto Runtime::sensor_covers(const SensorCoverage &coverage, fixed::Vec target, std::int64_t range) const -> bool
{
	return sensor_coverage_contains(coverage, target, range, regions);
}

auto Runtime::sensor_covers(EntityId ship, const std::string &id, fixed::Vec target, bool outer) const -> bool
{
	const auto coverage = sensor_coverage(ship, id, outer);
	return sensor_covers(coverage, target, outer ? coverage.outer : coverage.normal);
}

auto Runtime::sensor_contact(const SensorCoverage &coverage, fixed::Vec target, std::int64_t distance,
                             std::int64_t visibility) const -> SensorContact
{
	return sensor_coverage_contact(coverage, target, distance, visibility, regions);
}

auto Runtime::sensor_contact(EntityId ship, const std::string &id, fixed::Vec target, std::int64_t distance,
                             bool include_outer, std::int64_t visibility) const -> SensorContact
{
	return sensor_contact(sensor_coverage(ship, id, include_outer), target, distance, visibility);
}

auto Runtime::sensor_powered(EntityId ship, const std::string &id) const -> bool
{
	const auto fit = instance(entities.at(ship), id);
	if (!sensor_range(ship, id))
		return false;
	const auto &d = sensor_definition(ship, id);
	if (!d.turret.empty())
	{
		const auto mount = instance(entities.at(ship), d.turret);
		if (!mount->enabled || structurally_disabled(entities.at(ship), d.turret))
			return false;
		if (const auto *power = entities.at(ship).try_get<ShipResources>();
		    power && mount->entity.has<ResourceBinding>() &&
		    power->grants.at(mount->entity.get<ResourceBinding>().slot).supply_permille < 1000)
			return false;
	}
	if (const auto *power = entities.at(ship).try_get<ShipResources>(); power && fit->entity.has<ResourceBinding>())
		return power->grants.at(fit->entity.get<ResourceBinding>().slot).supply_permille == 1000;
	return true;
}

auto Runtime::sensor_target(EntityId ship, const std::string &id, std::int64_t value, bool execute) -> bool
{
	return directed_setting(ship, id, "target", value, execute);
}

auto Runtime::finding_available(const SensorFinding &f) const -> bool
{
	return (f.requires_event.empty() || (scenario && !scenario->runtime().events(f.requires_event).empty())) &&
	       (f.unless_event.empty() || !scenario || scenario->runtime().events(f.unless_event).empty());
}

auto Runtime::sensor_data(EntityId ship, EntityId target, const std::string &cap) const -> Json
{
	const auto entity = entities.at(target);
	Json data = Json::array();
	if (cap == "basic_identity")
	{
		const auto *membership = entity.try_get<FactionMembership>();
		const auto faction = membership ? membership->id : std::string{};
		const auto *own_membership = entities.at(ship).try_get<FactionMembership>();
		const auto own = own_membership ? own_membership->id : std::string{};
		return Json{{"class", definition(target).sensor_class},
		            {"faction", faction},
		            {"faction_label", faction.empty() ? "Unaffiliated" : factions.labels.at(faction)},
		            {"disposition", factions.relationship(own, faction)}};
	}
	if (cap == "compartment_condition")
	{
		if (const auto *structure = entity.try_get<CombatStructure>())
			for (std::size_t i = 0; i < structure->compartments.size(); ++i)
			{
				const auto &c = structure->compartments[i];
				const auto &d = structure->definition.compartments[i];
				data.push_back({{"id", d.id},
				                {"deck", d.deck},
				                {"hp", c.hp},
				                {"maximum", d.maximum},
				                {"disabled", c.disabled},
				                {"x_mm", d.x},
				                {"y_mm", d.y},
				                {"half_width_mm", d.half_width},
				                {"half_length_mm", d.half_length}});
			}
		return data;
	}
	if (cap == "shield_sectors")
		if (const auto *owner = entity.try_get<ShieldComponent>())
			for (const auto &c : owner->contributions)
				data.push_back({{"id", "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name},
				                {"contribution", c.definition.name},
				                {"bearing_mdeg", combat_angle(entity.get<Pose>().heading + c.definition.bearing)},
				                {"arc_mdeg", c.definition.arc},
				                {"strength", c.state.strength},
				                {"capacity", c.definition.shield.reserve.capacity},
				                {"enabled", shield_available(entity, c) && c.state.warmup > 0}});
	if (!entity.has<EquipmentIndex>())
		return data;
	for (auto child : entity.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;

			if (cap == "contact_arcs" && d.kind == OperatingSystem::Weapon)
			{
				auto heading = entity.get<Pose>().heading + d.bearing;
				const auto &w = d.get<WeaponDefinition>();
				if (!w.turret.empty())
					heading += instance(entity, w.turret)->entity.get<CombatMechanism>().get<TurretState>().heading;
				data.push_back({{"id", d.id}, {"bearing_mdeg", combat_angle(heading)}, {"arc_mdeg", d.arc}});
			}
		}
	return data;
}

auto Runtime::publish_sensor(EntityId ship, const std::string &system, EntityId target, const std::string &cap,
                             const Json &data, std::uint64_t tick) -> void
{
	if (!knowledge.contains_ship(ship))
		return;
	const auto &kd = knowledge.definition();
	const auto schema = kd.schema("phoenix.sensor." + cap);
	KnowledgeRecordInput record;
	record.schema = schema;
	record.producer = kd.role("phoenix.scenario");
	record.subject = target;
	record.provenance = {knowledge.system_id(ship, system), tick, tick, 0, EvidenceKind::Fact};
	record.fields[kd.field(schema, "payload")] = data.dump();
	for (const auto &console : definition(ship).consoles)
		if (std::find(console.feed_systems.begin(), console.feed_systems.end(), system) != console.feed_systems.end())
			knowledge.publish_scenario_console({ship, console.id}, record, tick);
}

auto Runtime::ghost_scan_duration(const GhostReception &g, const SensorDefinition &d) const -> std::int64_t
{
	if (!d.capabilities.contains("compartment_condition") && !d.capabilities.contains("findings"))
		return -1;
	return sensor_ghost_scan_duration(d, definition(g.source).psychic.power(g.power).scan_ticks);
}

auto Runtime::publish_track_findings(EntityId ship, const std::string &id, EntityId target, const SensorTrack &track)
    -> void
{
	const auto findings = sensor_track_findings(track);
	if (!findings.empty())
		publish_sensor(ship, id, target, "findings", findings, ecs.get<Clock>().tick);
}

auto Runtime::route_finding(EntityId ship, const std::string &system, const SensorFinding &finding, std::uint64_t tick,
                            bool announce) -> void
{
	if (finding.tag.empty() || !knowledge.contains_ship(ship))
		return;
	const auto &kd = knowledge.definition();
	const auto schema = kd.schema("phoenix.sensor.findings");
	KnowledgeRecordInput record;
	record.schema = schema;
	record.producer = kd.role("phoenix.scenario");
	record.subject = finding.target;
	record.provenance = {knowledge.system_id(ship, system), tick, tick, 0, EvidenceKind::Fact};
	const auto delivered = sensor_findings.forwarded_payload(ship, finding, tick);
	record.fields[kd.field(schema, "payload")] = delivered.dump();
	const auto source = system_owner(ship, system);
	for (const auto &console : definition(ship).consoles)
		if (console.station == finding.destination_station || console.id == finding.destination_station)
		{
			const ConsoleAddress destination{ship, console.id};
			knowledge.publish_scenario_console(destination, record, tick);
			bool human = false;
			for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
			{
				if (controller.definition.console == console.id && comms_controller(ship, controller.definition))
					human |= controller_operation_human(controller, "conversation");
				for (std::size_t channel = 0; channel < controller.definition.commands.size(); ++channel)
					for (const auto consumer : controller.definition.commands[channel].consumers)
						if (definition(ship).fitted->values.at(consumer).id == system)
							human |= controller_command_human(controller, channel);
			}
			if (announce && !human)
			{
				CoordinationInput message;
				message.sender_system = record.provenance.source;
				message.origin = ControlOrigin::Scenario;
				message.destination = {MessageDestinationKind::Console, knowledge.console_id(destination)};
				message.payload = record;
				message.presentation.title_key = "Scan finding forwarded";
				message.presentation.body_key = finding.text;
				knowledge.publish_scenario_message(source, std::move(message), tick);
			}
		}
}

auto Runtime::record_sensor_dispel(const GhostReception &g) -> void
{
	for (auto child : ordered_equipment(sensor_query, g.observer))
	{
		auto &tracks = child.get_mut<SensorRuntime>().tracks;
		if (!tracks.contains(g.id))
			continue;
		auto &track = tracks.at(g.id);
		record_sensor_dispel_sample(track,
		                            ghost_scan_duration(g, sensor_definition(g.observer, child.get<Equipment>().id)),
		                            ecs.get<Clock>().tick);
		publish_track_findings(g.observer, child.get<Equipment>().id, g.id, track);
	}
}

auto Runtime::advance_sensors(bool acquire_timed) -> void
{
	struct AcquisitionPort final : SensorAcquisitionPort
	{
		Impl &world;
		EntityId ship, target;
		const std::string &system;
		AcquisitionPort(Impl &owner, EntityId source, EntityId subject, const std::string &id)
		    : world(owner), ship(source), target(subject), system(id)
		{
		}
		Json read_capability(const std::string &capability) const override
		{
			return world.sensor_data(ship, target, capability);
		}
		void publish_capability(const std::string &capability, const Json &data, std::uint64_t tick) override
		{
			world.publish_sensor(ship, system, target, capability, data, tick);
		}
	};
	if (entities.empty())
		return;
	const auto tick = ecs.get<Clock>().tick;
	for (auto child : ordered_equipment(sensor_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		const auto &id = child.get<Equipment>().id;
		const auto d = sensor_acquisition_definition(ship, id);
		auto &state = child.get_mut<SensorRuntime>();
		if (child.has<DirectedRuntime>() && directed_operation(ship, id, DirectedOperationKind::Scan))
		{
			child.get_mut<DirectedRuntime>().ready = false;
			child.get_mut<DirectedRuntime>().blocker = "Target outside scanner coverage";
		}
		if (!sensor_powered(ship, id))
		{
			if (child.has<DirectedRuntime>() && directed_operation(ship, id, DirectedOperationKind::Scan))
				child.get_mut<DirectedRuntime>().blocker = "Scanner unavailable or unpowered";
			continue;
		}
		for_sensor_contacts(
		    ship, id,
		    [&](const SensorContactInput &input)
		    {
			    const auto &body = input.body;
			    const auto target = body.id;
			    auto *observed = observe_sensor_contact(state, body, input.apparent, tick);
			    if (!observed)
				    return;
			    auto &track = *observed;
			    AcquisitionPort port{*this, ship, target, id};
			    const auto acquire = [&](bool timed)
			    {
				    const std::string mode = timed ? directed_mode(ship, id)->id : std::string{};
				    acquire_sensor_capabilities(
				        state, track, {body, d, sensor_definition(ship, id), mode, tick, input.apparent, timed}, port);
				    bool finding_changed = false;
				    if (input.apparent && timed)
				    {
					    const auto &g = ghost_receptions.at(target);
					    const auto duration = ghost_scan_duration(g, d);
					    if (duration >= 0 &&
					        advance_ghost_acquisition(state, track, directed_mode(ship, id)->id, target, duration))
						    dispel_ghost(g);
				    }
				    if (!input.apparent && timed && d.capabilities.contains("findings"))
					    for (const auto &f : sensor_findings.definitions())
						    if (f.target == target && finding_available(f) &&
						        std::all_of(f.capabilities.begin(), f.capabilities.end(),
						                    [&](const auto &cap)
						                    {
							                    return d.capabilities.contains(cap);
						                    }))
						    {
							    const auto update =
							        sensor_findings.advance(state, track, directed_mode(ship, id)->id, ship, target, f,
							                                d.capabilities.at("findings"), tick);
							    if (!update.ready)
								    continue;
							    finding_changed |= update.changed;
							    const bool discovered = update.discovered;
							    if (discovered || tick % 20 == 0)
								    route_finding(ship, id, f, tick, discovered);
							    if (discovered && scenario && !f.event.empty())
								    scenario->runtime().emit({conversation_instance, ++conversation_event_sequence, 0},
								                             f.event,
								                             {{"ship", static_cast<std::int64_t>(ship)},
								                              {"amount", static_cast<std::int64_t>(target)},
								                              {"flag", true},
								                              {"detail", f.id}});
						    }
				    if (finding_changed || tick % 20 == 0)
					    publish_track_findings(ship, id, target, track);
			    };
			    if (!input.focused_only)
				    acquire(false);
			    if (acquire_timed && directed_target(ship, id) == target &&
			        directed_operation(ship, id, DirectedOperationKind::Scan))
				    execute_directed(entities.at(ship), child, DirectedOperationKind::Scan,
				                     directed_scan_covers(ship, id, body),
				                     [&]()
				                     {
					                     acquire(true);
					                     return true;
				                     });
		    },
		    false, acquire_timed);
	}
}

auto Runtime::sensor_aim(EntityId ship, const std::string &id, ConsoleContacts &cache)
    -> std::optional<std::pair<fixed::Vec, std::int64_t>>
{
	const auto &d = definition(ship).sensors.at(id);
	auto &state = instance(entities.at(ship), id)->entity.get_mut<SensorRuntime>();
	const auto target = directed_target(ship, id);
	if (!target || !sensor_powered(ship, id))
		return {};
	if (const auto aim = directed_contact(ship, id, target, cache); aim && !aim->bearing)
	{
		if (!state.tracks.contains(aim->body.id) && state.tracks.size() >= 256)
			return {};
		auto &track = state.tracks[aim->body.id];
		track.x = aim->body.x;
		track.y = aim->body.y;
		state.last_bearing.reset();
		return std::pair{fixed::Vec{aim->body.x, aim->body.y}, d.bearing};
	}
	for (const auto &b : console_detection(system_owner(ship, id), cache).bearing_contacts)
		if (b.id == target)
			state.last_bearing = b;
	if (state.last_bearing &&
	    (!state.tracks.contains(target) || state.last_bearing->observed_tick >= state.tracks.at(target).seen))
		return std::pair{sensor_origin(ship, d) +
		                     fixed::scale(fixed::direction(state.last_bearing->bearing_mdeg), sensor_range(ship, id)),
		                 d.bearing};
	if (state.tracks.contains(target))
	{
		const auto &t = state.tracks.at(target);
		return std::pair{fixed::Vec{t.x, t.y}, d.bearing};
	}
	return {};
}

auto Runtime::sensor_evidence(const ConsoleAddress &address) const -> std::map<EntityId, Json>
{
	std::vector<const SensorRuntime *> sources;
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (definition(address.ship).sensors.contains(id))
			sources.push_back(&instance(entities.at(address.ship), id)->entity.get<SensorRuntime>());
	return collect_sensor_evidence(address, sources, knowledge, ecs.get<Clock>().tick);
}

auto Runtime::sensor_projection(const ConsoleAddress &address, const std::map<EntityId, Json> &evidence) const -> Json
{
	std::vector<SensorScannerProjection> scanners;
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (definition(address.ship).sensors.contains(id))
		{
			SensorScannerProjection input;
			input.id = id;
			input.definition = sensor_acquisition_definition(address.ship, id);
			input.state = &instance(entities.at(address.ship), id)->entity.get<SensorRuntime>();
			input.selected = directed_target(address.ship, id);
			input.heading = sensor_heading(address.ship, input.definition);
			input.range = sensor_range(address.ship, id);
			input.origin = sensor_origin(address.ship, input.definition);
			input.powered = sensor_powered(address.ship, id);
			if (const auto *mode = directed_mode(address.ship, id))
				input.mode = mode->id;
			scanners.push_back(std::move(input));
		}
	return project_sensor_board(scanners, evidence, ecs.get<Clock>().tick);
}

auto Runtime::save_sensors() const -> Json
{
	Json out = Json::array();
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *state = child.try_get<SensorRuntime>())
					out.push_back(save_sensor_runtime(ship, child.get<Equipment>().id, *state));
	return out;
}

auto Runtime::restore_sensors(const Json &rows) -> void
{
	if (!rows.is_array() || rows.size() > 4096)
		throw std::runtime_error("Invalid sensor save");
	std::set<std::pair<EntityId, std::string>> seen;
	std::set<EntityId> physical_entities;
	for (const auto &[id, entity] : entities)
		physical_entities.insert(id);
	std::map<EntityId, std::int64_t> ghost_scan_ticks;
	for (const auto &[id, ghost] : ghost_receptions.records())
		ghost_scan_ticks.emplace(id, definition(ghost.source).psychic.power(ghost.power).scan_ticks);
	for (const auto &row : rows)
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !definition(ship).sensors.contains(id) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved scanner");
		if (row.contains("target"))
			throw std::runtime_error("Scanner intent belongs to the directed capability");
		const auto state =
		    read_sensor_runtime(row, {ship, directed_target(ship, id), ecs.get<Clock>().tick,
		                              sensor_definition(ship, id), directed_definition(ship, id), physical_entities,
		                              ghost_receptions.records(), ghost_scan_ticks, sensor_findings.definitions()});
		instance(entities.at(ship), id)->entity.set<SensorRuntime>(state);
	}
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (child.has<SensorRuntime>() && !seen.contains({ship, child.get<Equipment>().id}))
					throw std::runtime_error("Missing saved scanner acquisition state");
}
} // namespace phoenix::world_detail
