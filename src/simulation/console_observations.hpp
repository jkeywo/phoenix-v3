// Append contacts using only the permitted radar feeds supplied by the caller.
void append_sensor_contacts(const ConsoleAddress &address, Observation &result) const
{
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	std::int64_t range = 0;
	for (const auto &sensor : result.systems)
		if (sensor.kind == OperatingSystem::Radar && sensor.enabled)
			range = std::max(range, sensor.effective_range);
	if (range)
		contact_index.each(
		    Bounds::around(own.position, range),
		    [&](std::size_t row)
		    {
			    const auto &contact = contact_rows[row];
			    if (contact.id == address.ship)
				    return;
			    const auto &tags = definitions.at(contact.entity.get<Definition>().name).tags;
			    bool visible = false;
			    for (auto &sensor : result.systems)
				    if (sensor.kind == OperatingSystem::Radar && sensor.enabled &&
				        std::any_of(tags.begin(), tags.end(),
				                    [&](const auto &tag)
				                    {
					                    return std::find(sensor.shows.begin(), sensor.shows.end(), tag) !=
					                           sensor.shows.end();
				                    }) &&
				        scenario_sensor_visible(regions, {own.position.x, own.position.y},
				                                {contact.position.x, contact.position.y}, sensor.effective_range))
				    {
					    sensor.contact_ids.push_back(contact.id);
					    visible = true;
				    }
			    if (visible)
				    result.contacts.push_back(view(contact.id, contact.entity.get<Definition>(),
				                                   contact.entity.get<Pose>(), contact.entity.get<Motion>()));
		    });
	for (auto missile : projectile_rows())
	{
		const auto &p = missile.get<Projectile>();
		if (!p.definition.durability || missile.get<Destructible>().durability <= 0)
			continue;
		bool visible = false;
		const auto contact_id = 7000000000000000ULL + p.id;
		for (auto &sensor : result.systems)
			if (sensor.kind == OperatingSystem::Radar && sensor.enabled &&
			    std::find(sensor.shows.begin(), sensor.shows.end(), "missile") != sensor.shows.end() &&
			    scenario_sensor_visible(regions, {own.position.x, own.position.y}, {p.position.x, p.position.y},
			                            sensor.effective_range) &&
			    fixed::length(p.position - own.position) + std::abs(p.altitude) <= sensor.effective_range)
			{
				sensor.contact_ids.push_back(contact_id);
				visible = true;
			}
		if (visible)
			result.contacts.push_back(
			    {contact_id, p.definition.visual, p.position.x, p.position.y, p.velocity.x, p.velocity.y, p.heading});
	}
	const auto &own_faction = entity.get<FactionMembership>().id;
	for (auto &contact : result.contacts)
		if (const auto found = entities.find(contact.id); found != entities.end())
		{
			const auto *membership = found->second.try_get<FactionMembership>();
			contact.faction = membership ? membership->id : std::string{};
			contact.faction_label = contact.faction.empty() ? "Unaffiliated" : factions.labels.at(contact.faction);
			contact.disposition = factions.relationship(own_faction, contact.faction);
		}
	std::sort(result.contacts.begin(), result.contacts.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
}
std::vector<BodyView> capture_weapon_contacts(const ConsoleAddress &address) const
{
	Observation result;
	const auto entity = entities.at(address.ship);
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (const auto fit = instance(entity, id); fit && fit->radar)
		{
			SystemView sensor;
			sensor.kind = OperatingSystem::Radar;
			sensor.effective_range = sensor_range(address.ship, id);
			sensor.enabled = sensor.effective_range > 0;
			sensor.shows = definitions.at(entity.get<Definition>().name).radar_shows.at(id);
			result.systems.push_back(std::move(sensor));
		}
	append_sensor_contacts(address, result);
	return std::move(result.contacts);
}
// World::Impl observation producer. Human projection and decision snapshots share this boundary.
Observation capture_console_observation(const ConsoleAddress &address, Json gameplay = Json::object()) const
{
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	const auto &seat = *this->seat(address.ship, address.console);
	Observation result{view(address.ship, entity.get<Definition>(), own, entity.get<Motion>()), {}};
	result.own.faction = entity.get<FactionMembership>().id;
	result.own.faction_label = result.own.faction.empty() ? "Unaffiliated" : factions.labels.at(result.own.faction);
	result.own.disposition = factions.relationship(result.own.faction, result.own.faction);
	result.console = seat.id;
	result.regions = region_markers(regions);
	result.observed_tick = ecs.get<Clock>().tick;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.gameplay = std::move(gameplay);
	project_combat(address, result);
	result.engine_enabled = false;
	result.radar_enabled = false;
	for (const auto &id : seat.feed_systems)
	{
		const auto instance = this->instance(entity, id);
		SystemView state;
		state.id = id;
		state.controlled = false;
		const auto *engine = instance ? instance->engine : nullptr;
		const auto *turn = instance ? instance->steering : nullptr;
		const auto *radar = instance ? instance->radar : nullptr;
		if (engine)
		{
			state.kind = OperatingSystem::Engine;
			state.requested = engine->speed;
			state.maximum = engine->max_speed;
			state.enabled = engine->enabled;
		}
		else if (turn)
		{
			state.kind = OperatingSystem::Steering;
			state.requested = turn->heading;
			state.maximum = 359999;
		}
		else if (radar)
		{
			state.kind = OperatingSystem::Radar;
			state.maximum = radar->range;
			state.enabled = radar->enabled;
		}
		if (instance && entity.has<NamedEquipment>())
			state.telemetry = telemetry(address.ship, instance->ordinal);
		if (instance && instance->advanced)
		{
			const auto index = static_cast<std::size_t>(instance->ordinal);
			state.kind = instance->kind;
			state.enabled = state.telemetry.at("enabled") != 0;
			state.requested = state.telemetry.at("requested");
			if (auto runtime = gameplay_systems(entity); runtime && runtime->values[index].flight)
				state.maximum = state.kind == OperatingSystem::Steering ? 359999
				                : state.kind == OperatingSystem::Lateral
				                    ? runtime->values[index].flight->definition.lateral_speed
				                    : runtime->values[index].flight->definition.maximum_speed;
			else
				state.maximum = instance->maximum;
		}
		const auto &controls = entity.get<EntityControls>();
		if (controls.layout.converted && instance)
			for (const auto &controller : controls.controllers)
				if (controller.definition.console == address.console && controller.human)
					for (const auto &command : controller.definition.commands)
						if (std::find(command.consumers.begin(), command.consumers.end(), instance->ordinal) !=
						    command.consumers.end())
							state.controlled = true;
		if (state.kind == OperatingSystem::Radar)
		{
			state.shows = definitions.at(entity.get<Definition>().name).radar_shows.at(id);
			state.effective_range = sensor_range(address.ship, id);
			const auto &envelope = sensor_envelope(address.ship, id, state.effective_range);
			state.radial_ranges = envelope.distances;
			state.reach_profile = envelope.profile;
			state.reach_uncertainty_mm = envelope.uncertainty;
			state.reach_limited = envelope.limited;
		}
		if (instance && structurally_disabled(entity, id))
		{
			state.enabled = false;
			state.status = state.telemetry["destroyed"] ? "Destroyed"
			               : state.telemetry["damaged"] ? "Damaged"
			                                            : "Disabled: compartment structure";
		}
		result.systems.push_back(std::move(state));
	}
	if (entity.has<NamedEquipment>())
	{
		std::int64_t range = 0;
		for (const auto &state : result.systems)
		{
			if (state.kind == OperatingSystem::Engine)
			{
				result.requested_speed = state.requested;
				result.engine_enabled = state.enabled;
			}
			else if (state.kind == OperatingSystem::Steering)
				result.requested_heading = state.requested;
			else if (state.kind == OperatingSystem::Radar && state.enabled)
				range = std::max(range, sensor_range(address.ship, state.id));
		}
		result.radar_enabled = range > 0;
		append_sensor_contacts(address, result);
		return result;
	}
	return result;
}
