// World::Impl observation producer. Human projection and decision snapshots share this boundary.
Observation capture_console_observation(const ConsoleAddress &address) const
{
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	const auto &seat = *this->seat(address.ship, address.console);
	Observation result{view(address.ship, entity.get<Definition>(), own, entity.get<Motion>()), {}};
	result.console = seat.id;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.engine_enabled = false;
	result.radar_enabled = false;
	for (const auto &id : seat.feed_systems)
	{
		const auto instance = this->instance(entity, id);
		SystemView state;
		state.id = id;
		state.controlled = std::find(seat.controlled_systems.begin(), seat.controlled_systems.end(), id) !=
		                   seat.controlled_systems.end();
		const auto *engine = instance         ? (instance->engine ? &*instance->engine : nullptr)
		                     : id == "engine" ? legacy_component<Engine>(entity)
		                                      : nullptr;
		const auto *turn = instance           ? (instance->steering ? &*instance->steering : nullptr)
		                   : id == "steering" ? legacy_component<Steering>(entity)
		                                      : nullptr;
		const auto *radar = instance        ? (instance->radar ? &*instance->radar : nullptr)
		                    : id == "radar" ? legacy_component<Radar>(entity)
		                                    : nullptr;
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
		if (instance && instance->advanced)
		{
			const auto index = static_cast<std::size_t>(instance->ordinal);
			state.kind = instance->kind;
			state.telemetry = telemetry(address.ship, index);
			state.enabled = state.telemetry.at("enabled") != 0;
			state.requested = state.telemetry.at("requested");
			state.controlled = instance->held;
			if (auto runtime = gameplay_systems(entity); runtime && runtime->values[index].flight)
				state.maximum = state.kind == OperatingSystem::Steering ? 359999
				                : state.kind == OperatingSystem::Lateral
				                    ? runtime->values[index].flight->definition.lateral_speed
				                    : runtime->values[index].flight->definition.maximum_speed;
			else
				state.maximum = instance->maximum;
			const auto runtime = (*gameplay_systems(entity)).values[index];
			state.status = runtime.result.reason;
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
		if (range)
			contact_index.each(
			    Bounds::around(own.position, range),
			    [&](std::size_t row)
			    {
				    const auto &contact = contact_rows[row];
				    if (contact.id != address.ship && fixed::length(contact.position - own.position) <= range)
					    result.contacts.push_back(view(contact.id, contact.entity.get<Definition>(),
					                                   contact.entity.get<Pose>(), contact.entity.get<Motion>()));
			    });
		std::sort(result.contacts.begin(), result.contacts.end(),
		          [](const auto &a, const auto &b)
		          {
			          return a.id < b.id;
		          });
		return result;
	}
	if (auto engine = legacy_component<Engine>(entity); engine && (seat.feeds & system_bit(OperatingSystem::Engine)))
	{
		result.requested_speed = engine->speed;
		result.engine_enabled = engine->enabled;
	}
	if (auto turn = legacy_component<Steering>(entity); turn && (seat.feeds & system_bit(OperatingSystem::Steering)))
		result.requested_heading = turn->heading;
	if (!(seat.feeds & system_bit(OperatingSystem::Radar)))
		return result;
	auto radar = legacy_component<Radar>(entity);
	result.radar_enabled = radar ? radar->enabled : operation(entity).radar_enabled;
	if (!radar || !radar->enabled)
		return result;
	contact_index.each(
	    Bounds::around(own.position, radar->range),
	    [&](std::size_t row)
	    {
		    const auto &contact = contact_rows[row];
		    if (contact.id != address.ship && fixed::length(contact.position - own.position) <= radar->range)
			    result.contacts.push_back(view(contact.id, contact.entity.get<Definition>(), contact.entity.get<Pose>(),
			                                   contact.entity.get<Motion>()));
	    });
	std::sort(result.contacts.begin(), result.contacts.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	return result;
}
