module;
#include "native_headers.hpp"

module phoenix.simulation;
import :sensor_bindings;

namespace phoenix::world_detail
{
auto Runtime::sensor_range(EntityId ship, const std::string &name, bool outer) const -> std::int64_t
{
	const auto entity = entities.at(ship);
	const auto sensor = instance(entity, name);
	if (!sensor || !sensor->radar || !sensor->enabled || !sensor->radar->enabled || structurally_disabled(entity, name))
		return 0;
	auto range =
	    outer ? std::max(sensor->radar->range, sensor_definition(ship, name).long_range) : sensor->radar->range;
	if (sensor->kind == OperatingSystem::Weapon && directed_definition(ship, name))
	{
		const auto *mode = directed_mode(ship, name);
		if (!mode || mode->operation != DirectedOperationKind::Scan)
			return 0;
		range = mode->range;
	}
	range = effective_property(entity, name, "sensors", range);
	if (auto systems = gameplay_systems(entity); systems && systems->power)
	{
		const auto index = sensor->entity.get<ResourceBinding>().slot;
		const auto &power = *systems->power;
		const auto &binding = power.definition.systems[index];
		if (binding.store != no_resource)
		{
			const auto &store = power.network.stores()[binding.store];
			if (!store.enabled || (store.thermal_stop && !store.thermal_override))
				return 0;
		}
		range = range * power.grants[index].supply_permille / 1000;
	}
	return range;
}
auto Runtime::sensor_envelope(EntityId ship, const std::string &name, std::int64_t range) const
    -> const SensorEnvelope &
{
	return sensor_envelopes.get(ship, name, entities.at(ship).get<Pose>().position, range, regions);
}

} // namespace phoenix::world_detail
