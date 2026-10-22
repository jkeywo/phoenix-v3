// World adapter: physical coverage, retained evidence and permitted projection.
std::vector<SensorFinding> sensor_findings;
std::set<std::tuple<EntityId, EntityId, std::string>> sensor_discoveries;
const SensorDefinition &sensor_definition(EntityId ship, const std::string &id) const
{
	return definition(ship).sensors.at(id);
}
std::int64_t sensor_heading(EntityId ship, const SensorDefinition &d) const
{
	auto heading = entities.at(ship).get<Pose>().heading + d.bearing;
	if (!d.turret.empty())
		heading += instance(entities.at(ship), d.turret)->entity.get<CombatMechanism>().get<TurretState>().heading;
	return combat_angle(heading);
}
fixed::Vec sensor_origin(EntityId ship, const SensorDefinition &d) const
{
	const auto entity = entities.at(ship);
	if (d.turret.empty())
		return entity.get<Pose>().position;
	const auto &m = instance(entity, d.turret)->entity.get<CombatMechanism>().definition;
	return combat_world({m.x, m.y}, entity.get<Pose>());
}
bool sensor_covers(EntityId ship, const std::string &id, fixed::Vec target, bool outer = false) const
{
	const auto &d = sensor_definition(ship, id);
	const auto source = entities.at(ship);
	if (!d.turret.empty() && (!instance(source, d.turret)->enabled || structurally_disabled(source, d.turret)))
		return false;
	const auto range = sensor_range(ship, id, outer);
	if (!range)
		return false;
	const auto origin = sensor_origin(ship, d);
	if (!scenario_sensor_visible(regions, {origin.x, origin.y}, {target.x, target.y}, range))
		return false;
	return d.arc == 360000 ||
	       std::abs(combat_delta(combat_bearing(target - origin), sensor_heading(ship, d))) <= d.arc / 2;
}
enum class SensorContact
{
	None,
	Bearing,
	Position
};
SensorContact sensor_contact(EntityId ship, const std::string &id, fixed::Vec target, std::int64_t distance) const
{
	const auto normal = sensor_range(ship, id);
	if (distance <= normal && sensor_covers(ship, id, target))
		return SensorContact::Position;
	const auto outer = sensor_range(ship, id, true);
	if (outer > normal && distance <= outer && sensor_covers(ship, id, target, true))
		return SensorContact::Bearing;
	return SensorContact::None;
}
bool sensor_powered(EntityId ship, const std::string &id) const
{
	const auto fit = instance(entities.at(ship), id);
	if (!sensor_range(ship, id))
		return false;
	const auto &d = sensor_definition(ship, id);
	if (!d.turret.empty())
	{
		const auto mount = instance(entities.at(ship), d.turret);
		if (!mount->enabled || structurally_disabled(entities.at(ship), d.turret) ||
		    fault_effectiveness(entities.at(ship), d.turret) == 0)
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
bool sensor_target(EntityId ship, const std::string &id, std::int64_t value, bool execute)
{
	if (value < 0 || static_cast<EntityId>(value) == ship || !definition(ship).sensors.contains(id))
		return false;
	auto &state = instance(entities.at(ship), id)->entity.get_mut<SensorRuntime>();
	if (value)
	{
		const auto owner = system_owner(ship, id);
		const auto detected = capture_sensor_observation(owner);
		const auto &contacts = detected.contacts;
		if (!state.tracks.contains(value) &&
		    !(state.last_bearing && state.last_bearing->id == static_cast<EntityId>(value)) &&
		    std::none_of(detected.bearing_contacts.begin(), detected.bearing_contacts.end(),
		                 [&](const auto &b)
		                 {
			                 return b.id == static_cast<EntityId>(value);
		                 }) &&
		    std::none_of(contacts.begin(), contacts.end(),
		                 [&](const auto &c)
		                 {
			                 return c.id == static_cast<EntityId>(value);
		                 }))
			return false;
	}
	if (execute)
	{
		if (!value || state.target != static_cast<EntityId>(value))
			state.last_bearing.reset();
		state.target = value;
	}
	return true;
}
bool finding_available(const SensorFinding &f) const
{
	return (f.requires_event.empty() || (scenario && !scenario->runtime().events(f.requires_event).empty())) &&
	       (f.unless_event.empty() || !scenario || scenario->runtime().events(f.unless_event).empty());
}
Json sensor_data(EntityId ship, EntityId target, const std::string &cap) const
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
	for (auto child : entity.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>())
		{
			const auto &d = m->definition;
			if (cap == "shield_sectors" && d.kind == OperatingSystem::Shield)
			{
				const auto &s = m->get<ShieldState>();
				data.push_back({{"id", d.id},
				                {"bearing_mdeg", combat_angle(entity.get<Pose>().heading + d.bearing)},
				                {"arc_mdeg", d.arc},
				                {"strength", s.strength},
				                {"capacity", d.get<ShieldDefinition>().reserve.capacity},
				                {"enabled", child.get<Equipment>().enabled && !structurally_disabled(entity, d.id)}});
			}
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
void publish_sensor(EntityId ship, const std::string &system, EntityId target, const std::string &cap, const Json &data,
                    std::uint64_t tick)
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
void advance_sensors(bool acquire_timed = true)
{
	if (entities.empty())
		return;
	const auto tick = ecs.get<Clock>().tick;
	for (auto child : ordered_equipment(sensor_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		const auto &id = child.get<Equipment>().id;
		const auto &d = sensor_definition(ship, id);
		auto &state = child.get_mut<SensorRuntime>();
		if (!sensor_powered(ship, id))
			continue;
		const auto origin = sensor_origin(ship, d);
		contact_index.each(Bounds::around(origin, sensor_range(ship, id)),
		                   [&](std::size_t row)
		                   {
			                   const auto &contact = contact_rows[row];
			                   if (contact.id == ship || !sensor_covers(ship, id, contact.position) ||
			                       distance_3d(ship, contact.id) > sensor_range(ship, id))
				                   return;
			                   const auto &tags = definition(contact.id).tags;
			                   const auto &shows = definition(ship).radar_shows.at(id);
			                   if (std::none_of(tags.begin(), tags.end(),
			                                    [&](const auto &tag)
			                                    {
				                                    return std::find(shows.begin(), shows.end(), tag) != shows.end();
			                                    }))
				                   return;
			                   if (!state.tracks.contains(contact.id) && state.tracks.size() >= 256)
				                   return;
			                   auto &track = state.tracks[contact.id];
			                   track.x = contact.position.x;
			                   track.y = contact.position.y;
			                   track.seen = tick;
			                   for (const auto &[cap, duration] : d.capabilities)
			                   {
				                   if (cap == "own_arcs" || cap == "findings" ||
				                       (duration > 0 && (!acquire_timed || state.target != contact.id)))
					                   continue;
				                   auto &sample = track.samples[cap];
				                   sample.exposure = std::min(duration, sample.exposure + 1);
				                   if (sample.exposure < duration)
					                   continue;
				                   const auto data = sensor_data(ship, contact.id, cap);
				                   const bool changed = sample.data != data;
				                   sample.data = data;
				                   sample.observed = tick;
				                   if (changed || tick % 20 == 0)
					                   publish_sensor(ship, id, contact.id, cap, data, tick);
			                   }
			                   bool finding_changed = false;
			                   if (acquire_timed && state.target == contact.id && d.capabilities.contains("findings"))
				                   for (const auto &f : sensor_findings)
					                   if (f.target == contact.id && finding_available(f) &&
					                       std::all_of(f.capabilities.begin(), f.capabilities.end(),
					                                   [&](const auto &cap)
					                                   {
						                                   return d.capabilities.contains(cap);
					                                   }))
					                   {
						                   auto &sample = track.samples["finding:" + f.id];
						                   const auto duration = std::max(f.ticks, d.capabilities.at("findings"));
						                   sample.exposure = std::min(duration, sample.exposure + 1);
						                   if (sample.exposure < duration)
							                   continue;
						                   finding_changed |= sample.data.is_null();
						                   sample.data = Json{{"id", f.id}, {"text", f.text}};
						                   sample.observed = tick;
						                   if (sensor_discoveries.emplace(ship, contact.id, f.id).second && scenario &&
						                       !f.event.empty())
							                   scenario->runtime().emit(
							                       {conversation_instance, ++conversation_event_sequence, 0}, f.event,
							                       {{"ship", static_cast<std::int64_t>(ship)},
							                        {"amount", static_cast<std::int64_t>(contact.id)},
							                        {"flag", true},
							                        {"detail", f.id}});
					                   }
			                   Json findings = Json::array();
			                   for (const auto &[key, sample] : track.samples)
				                   if (key.starts_with("finding:") && !sample.data.is_null())
					                   findings.push_back(Json{{"id", sample.data.at("id")},
					                                           {"text", sample.data.at("text")},
					                                           {"observed", sample.observed}});
			                   if (!findings.empty() && (finding_changed || tick % 20 == 0))
				                   publish_sensor(ship, id, contact.id, "findings", findings, tick);
		                   });
	}
}
// Sensor mounts use only current permitted returns or their last observation.
std::optional<std::pair<fixed::Vec, std::int64_t>> sensor_aim(EntityId ship, const std::string &turret,
                                                              ConsoleContacts &cache)
{
	for (const auto &[id, d] : definition(ship).sensors)
		if (d.turret == turret)
		{
			auto &state = instance(entities.at(ship), id)->entity.get_mut<SensorRuntime>();
			if (!state.target || !sensor_powered(ship, id))
				return {};
			const auto &contacts = console_contacts(system_owner(ship, id), cache);
			for (const auto &contact : contacts)
				if (contact.id == state.target)
				{
					if (!state.tracks.contains(contact.id) && state.tracks.size() >= 256)
						return {};
					auto &track = state.tracks[contact.id];
					track.x = contact.x;
					track.y = contact.y;
					state.last_bearing.reset();
					return std::pair{fixed::Vec{contact.x, contact.y}, d.bearing};
				}
			for (const auto &b : console_detection(system_owner(ship, id), cache).bearing_contacts)
				if (b.id == state.target)
					state.last_bearing = b;
			if (state.last_bearing && (!state.tracks.contains(state.target) ||
			                           state.last_bearing->observed_tick >= state.tracks.at(state.target).seen))
				return std::pair{
				    sensor_origin(ship, d) +
				        fixed::scale(fixed::direction(state.last_bearing->bearing_mdeg), sensor_range(ship, id)),
				    d.bearing};
			if (state.tracks.contains(state.target))
			{
				const auto &t = state.tracks.at(state.target);
				return std::pair{fixed::Vec{t.x, t.y}, d.bearing};
			}
		}
	return {};
}
// One permitted evidence view supplies both radar identity and detailed readouts.
std::map<EntityId, Json> sensor_evidence(const ConsoleAddress &address) const
{
	std::map<EntityId, Json> contacts;
	std::map<EntityId, std::uint64_t> positions;
	const auto tick = ecs.get<Clock>().tick;
	auto contact = [&](EntityId target) -> Json &
	{
		auto &row = contacts[target];
		if (row.is_null())
			row = Json{{"id", target}, {"findings", Json::object()}};
		return row;
	};
	auto finding = [&](EntityId target, const std::string &cap, const Json &data, std::uint64_t observed, bool live)
	{
		auto &value = contact(target)["findings"][cap];
		if (value.is_null() || value.at("observed") < observed ||
		    (value.at("observed") == observed && live && !value.at("live").get<bool>()))
			value = Json{{"data", data}, {"observed", observed}, {"live", live}};
	};
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (definition(address.ship).sensors.contains(id))
			for (const auto &[target, track] :
			     instance(entities.at(address.ship), id)->entity.get<SensorRuntime>().tracks)
			{
				auto &row = contact(target);
				if (!positions.contains(target) || positions.at(target) < track.seen)
				{
					row["x"] = track.x;
					row["y"] = track.y;
					positions[target] = track.seen;
				}
				for (const auto &[cap, sample] : track.samples)
					if (!sample.data.is_null())
						finding(target, cap, sample.data, sample.observed, sample.observed == tick);
			}
	if (knowledge.contains_ship(address.ship))
	{
		const auto projection = knowledge.project(address);
		const auto &kd = knowledge.definition();
		for (const auto *records : {&projection.records, &projection.shared})
			for (const auto &record : *records)
			{
				const auto &name = kd.schemas.at(record.value.schema - 1).name;
				if (!name.starts_with("phoenix.sensor."))
					continue;
				const auto field = kd.field(record.value.schema, "payload");
				if (!record.value.fields.contains(field))
					continue;
				const auto cap = name.substr(15);
				const auto payload = Json::parse(std::get<std::string>(record.value.fields.at(field)));
				if (cap == "findings")
					for (const auto &entry : payload)
						finding(record.value.subject, "finding:" + entry.at("id").get<std::string>(),
						        Json{{"id", entry.at("id")}, {"text", entry.at("text")}}, entry.at("observed"), false);
				else
					finding(record.value.subject, cap, payload, record.value.provenance.observed_tick, false);
			}
	}
	return contacts;
}
Json sensor_projection(const ConsoleAddress &address, const std::map<EntityId, Json> &evidence) const
{
	Json out{{"scanners", Json::array()}, {"contacts", Json::array()}};
	const auto tick = ecs.get<Clock>().tick;
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (definition(address.ship).sensors.contains(id))
		{
			const auto &d = sensor_definition(address.ship, id);
			const auto &s = instance(entities.at(address.ship), id)->entity.get<SensorRuntime>();
			Json scanner{{"id", id},
			             {"target", s.target},
			             {"bearing_mdeg", sensor_heading(address.ship, d)},
			             {"arc_mdeg", d.arc},
			             {"range_mm", sensor_range(address.ship, id)},
			             {"x", sensor_origin(address.ship, d).x},
			             {"y", sensor_origin(address.ship, d).y},
			             {"blocker", ""},
			             {"progress", Json::array()}};
			if (!sensor_powered(address.ship, id))
				scanner["blocker"] = "Scanner unavailable or unpowered";
			else if (s.target && (!s.tracks.contains(s.target) || s.tracks.at(s.target).seen != tick))
				scanner["blocker"] = "Target outside scanner coverage";
			for (const auto &[target, track] : s.tracks)
			{
				for (const auto &[cap, sample] : track.samples)
				{
					if (target == s.target && d.capabilities.contains(cap) && d.capabilities.at(cap) > 0)
						scanner["progress"].push_back(
						    {{"capability", cap}, {"ticks", sample.exposure}, {"required", d.capabilities.at(cap)}});
					if (target == s.target && cap.starts_with("finding:"))
						for (const auto &f : sensor_findings)
							if ("finding:" + f.id == cap && finding_available(f))
							{
								auto &progress = scanner["progress"];
								auto found = std::find_if(progress.begin(), progress.end(),
								                          [](const auto &p)
								                          {
									                          return p.at("capability") == "findings";
								                          });
								const auto required = std::max(f.ticks, d.capabilities.at("findings"));
								if (found == progress.end())
									progress.push_back({{"capability", "findings"},
									                    {"ticks", sample.exposure},
									                    {"required", required}});
								else
								{
									(*found)["ticks"] = found->at("ticks").get<std::int64_t>() + sample.exposure;
									(*found)["required"] = found->at("required").get<std::int64_t>() + required;
								}
							}
				}
			}
			for (const auto &[cap, required] : d.capabilities)
				if (required > 0 && cap != "findings" && cap != "own_arcs" &&
				    std::none_of(scanner["progress"].begin(), scanner["progress"].end(),
				                 [&](const auto &p)
				                 {
					                 return p.at("capability") == cap;
				                 }))
					scanner["progress"].push_back({{"capability", cap}, {"ticks", 0}, {"required", required}});
			out["scanners"].push_back(scanner);
		}
	for (const auto &[target, row] : evidence)
		out["contacts"].push_back(row);
	return out;
}
Json save_sensors() const
{
	Json out = Json::array();
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *s = child.try_get<SensorRuntime>())
				{
					Json tracks = Json::array();
					for (const auto &[target, t] : s->tracks)
						tracks.push_back(Json{{"target", target}, {"state", t}});
					out.push_back({{"ship", ship},
					               {"system", child.get<Equipment>().id},
					               {"target", s->target},
					               {"tracks", tracks},
					               {"last_bearing",
					                s->last_bearing ? projection::bearing_json(*s->last_bearing) : Json(nullptr)}});
				}
	return out;
}
void restore_sensors(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 4096)
		throw std::runtime_error("Invalid sensor save");
	std::set<std::pair<EntityId, std::string>> seen;
	for (const auto &row : rows)
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !definition(ship).sensors.contains(id) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved scanner");
		SensorRuntime state;
		state.target = row.at("target");
		if (state.target == ship)
			throw std::runtime_error("Invalid saved scan target");
		auto read_bearing = [&](const Json &b)
		{
			if (!b.at("id").is_number_integer() || b.at("id") <= 0 || !b.at("observed_tick").is_number_integer() ||
			    b.at("observed_tick") < 0)
				throw std::runtime_error("Invalid saved bearing");
			BearingContactView value{b.at("id").get<EntityId>(),
			                         checked_integer(b.at("bearing_mdeg"), 0, 359999, "sensor.bearing"),
			                         b.at("observed_tick").get<std::uint64_t>()};
			if (value.id == ship || value.observed_tick > ecs.get<Clock>().tick)
				throw std::runtime_error("Invalid saved bearing");
			return value;
		};
		const auto bearings = row.value("bearings", Json::array());
		if (!bearings.is_array() || bearings.size() > 256)
			throw std::runtime_error("Invalid saved bearings");
		std::set<EntityId> bearing_ids;
		for (const auto &b : bearings)
		{
			const auto value = read_bearing(b);
			if (!bearing_ids.insert(value.id).second)
				throw std::runtime_error("Duplicate saved bearing");
			if (!row.contains("last_bearing") && value.id == state.target)
				state.last_bearing = value;
		}
		if (row.contains("last_bearing") && !row.at("last_bearing").is_null())
		{
			const auto value = read_bearing(row.at("last_bearing"));
			if (value.id != state.target)
				throw std::runtime_error("Saved bearing does not match scan target");
			state.last_bearing = value;
		}
		if (row.at("tracks").size() > 256)
			throw std::runtime_error("Too many saved sensor tracks");
		for (const auto &entry : row.at("tracks"))
		{
			const EntityId target = entry.at("target");
			auto track = entry.at("state").get<SensorTrack>();
			if (!target || target == ship || track.samples.size() > 134 || track.seen > ecs.get<Clock>().tick ||
			    !state.tracks.emplace(target, track).second)
				throw std::runtime_error("Invalid saved sensor track");
			for (const auto &[key, sample] : track.samples)
			{
				const auto &d = sensor_definition(ship, id);
				std::int64_t required = -1;
				if (d.capabilities.contains(key) && key != "own_arcs" && key != "findings")
					required = d.capabilities.at(key);
				if (d.capabilities.contains("findings"))
					for (const auto &f : sensor_findings)
						if (key == "finding:" + f.id && f.target == target &&
						    std::all_of(f.capabilities.begin(), f.capabilities.end(),
						                [&](const auto &cap)
						                {
							                return d.capabilities.contains(cap);
						                }))
							required = std::max(f.ticks, d.capabilities.at("findings"));
				if (required < 0 || sample.exposure > required ||
				    (!sample.data.is_null() && sample.exposure < required))
					throw std::runtime_error("Unearned or unknown saved sensor capability");

				if (sample.exposure < 0 || sample.exposure > 1728000 || sample.observed > ecs.get<Clock>().tick ||
				    sample.data.dump().size() > 131072)
					throw std::runtime_error("Invalid saved sensor sample");
			}
		}
		instance(entities.at(ship), id)->entity.set<SensorRuntime>(state);
	}
}
