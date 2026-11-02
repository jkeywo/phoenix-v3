module;
#include "native_headers.hpp"
module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::capture_craft_self_decision(const ConsoleAddress &address, const CombatDecisionSnapshot &combat) const
    -> std::optional<CraftSelfDecision>
{
	const auto entity = entities.at(address.ship);
	const auto *craft = entity.try_get<CraftState>();
	if (!craft)
		return std::nullopt;
	std::int64_t energy = 0, capacity = 0;
	if (const auto *resources = entity.try_get<ShipResources>())
		for (std::size_t i = 0; i < resources->network.stores().size(); ++i)
		{
			energy += resources->network.stores()[i].energy();
			capacity += resources->definition.network.stores[i].capacity;
		}
	CraftSelfDecision row;
	for (const auto &weapon : combat.weapons)
		if (weapon.enabled)
		{
			row.armed |= weapon.ammunition_free || weapon.loaded_quantity > 0;
			for (const auto &option : weapon.ammunition_options)
				row.armed |= option.available;
		}
	const auto anchor = craft->target == craft->home ? craft->rendezvous : craft->target_position;
	row.order = craft->order;
	row.target = craft->target;
	row.reserve_low = capacity > 0 && energy * 1000 <= capacity * definition(address.ship).craft->return_reserve;
	row.anchor_x = anchor.x;
	row.anchor_y = anchor.y;
	row.engagement_range = definition(address.ship).craft->engagement_range;
	row.pursuit_altitude_offset = definition(address.ship).craft->cruise_altitude + craft_flight_band(address.ship);
	if (const auto *g = entity.try_get<CraftGuidance>())
		row.guidance = CraftGuidanceDecision{g->destination.x, g->destination.y, g->altitude, g->speed,
		                                     g->heading,       g->target,        g->navigate};
	return row;
}
} // namespace phoenix::world_detail
