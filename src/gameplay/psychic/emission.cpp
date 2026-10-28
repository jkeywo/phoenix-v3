module;
#include <map>
#include <nlohmann/json.hpp>
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <functional>
module phoenix.gameplay.psychic.emission;
import phoenix.gameplay.fitting;
import phoenix.gameplay.directed;
import phoenix.gameplay.combat.faults;
import phoenix.resources.equipment;
namespace phoenix
{
std::int64_t native_psychic_emission(flecs::entity entity, EntityId ship, bool unavailable, bool recent_weapon,
                                     const ConversationRuntime &conversations, std::uint64_t tick)
{
	std::int64_t result = 0;
	for (auto child : entity.get<EquipmentIndex>().values)
	{
		const auto &e = child.get<Equipment>();
		if (!e.enabled)
			continue;
		if (const auto *structure = entity.try_get<CombatStructure>())
		{
			const auto fit = find_equipment(entity, e.id);
			if (equipment_structurally_disabled(structure, e.id, fit && fit->kind == OperatingSystem::Team,
			                                    unavailable))
				continue;
		}
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
		if (child.has<Radar>() && child.get<Radar>().enabled)
		{
			const auto fit = find_equipment(entity, e.id);
			const auto *directed = fit ? fit->entity.try_get<DirectedRuntime>() : nullptr;
			if (directed && directed->target)
				result += funded / 4;
		}
	}
	if (recent_weapon)
		result += 500;
	for (const auto &[id, c] : conversations.conversations)
		for (const auto &m : c.messages)
			if (m.sender == "ship:" + std::to_string(ship) && m.sent + 20 >= tick)
				result += 250;
	return std::min<std::int64_t>(1000, result);
}

void PsychicEmissionMemory::record(EntityId ship, std::uint64_t tick)
{
	emissions_[ship] = tick + 20;
}
bool PsychicEmissionMemory::recent(EntityId ship, std::uint64_t tick) const
{
	return emissions_.contains(ship) && emissions_.at(ship) > tick;
}
void PsychicEmissionMemory::expire(std::uint64_t tick, const std::function<bool(EntityId)> &actor_exists)
{
	std::erase_if(emissions_,
	              [&](const auto &entry)
	              {
		              return !actor_exists(entry.first) || entry.second <= tick;
	              });
}
Json PsychicEmissionMemory::save() const
{
	Json rows = Json::array();
	for (const auto &[ship, until] : emissions_)
		rows.push_back({{"ship", ship}, {"until", until}});
	return rows;
}
void PsychicEmissionMemory::restore(const Json &rows, std::uint64_t tick,
                                    const std::function<bool(EntityId)> &actor_exists)
{
	for (const auto &row : rows)
	{
		const EntityId ship = row.at("ship");
		const std::uint64_t until = row.at("until");
		if (!actor_exists(ship) || until > tick + 20 || !emissions_.emplace(ship, until).second)
			throw std::runtime_error("Invalid saved emission");
	}
}
} // namespace phoenix
