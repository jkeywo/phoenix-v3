module;
#include "native_headers.hpp"

module phoenix.simulation;
import :effect_work;

namespace phoenix::world_detail
{
auto Runtime::refresh_ability_work(bool advance) -> void
{
	for (const auto &[ship, entity] : entities)
	{
		if (!entity.has<AbilityRuntime>())
			continue;
		AbilityLifecycleBridge native(*this, ship);
		AbilityLifecycle lifecycle(entity, *definition(ship).abilities.catalog, ecs.get<Clock>().tick, native);
		lifecycle.refresh_work(advance);
	}
}
} // namespace phoenix::world_detail
