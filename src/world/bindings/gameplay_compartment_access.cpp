module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::compartment_edge_key(std::size_t a, std::size_t b) const -> std::string
{
	return std::to_string(std::min(a, b)) + ":" + std::to_string(std::max(a, b));
}

auto Runtime::logistics_revision(flecs::entity e) const -> std::uint64_t
{
	if (const auto *inventory = e.try_get<MaterialInventory>())
		return inventory->revision;
	if (const auto *structure = e.try_get<CombatStructure>())
		return structure->access_revision;
	return 1;
}

auto Runtime::physical_edge_open(EntityId id, std::size_t from, std::size_t to) const -> bool
{
	const auto *structure = entities.at(id).try_get<CombatStructure>();
	return !structure || !structure->closed_routes.contains(compartment_edge_key(from, to));
}

auto Runtime::compartment_access_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai)
    -> bool
{
	const auto e = entities.at(a.ship);
	const auto *structure = e.try_get<CombatStructure>();
	if (!structure || structure->definition.access_controller.empty() ||
	    !material_authority(a, c, structure->definition.access_controller, ai))
		return false;
	const auto fields = material_fields(c.text);
	const auto &compartments = structure->definition.compartment_ids;
	if (fields.size() != 2 || !compartments.contains(c.system) || !compartments.contains(fields[0]) ||
	    (fields[1] != "open" && fields[1] != "close"))
		return false;
	const auto from = compartments.at(c.system), to = compartments.at(fields[0]);
	bool exists = false;
	for (const auto &edge : structure->definition.connections)
		exists |= (edge.from == from && edge.to == to) || (edge.from == to && edge.to == from);
	if (!exists)
		return false;
	if (execute)
	{
		auto &state = e.get_mut<CombatStructure>();
		const auto key = compartment_edge_key(from, to);
		if (fields[1] == "close")
			state.closed_routes.insert(key);
		else
			state.closed_routes.erase(key);
		if (e.has<MaterialInventory>())
			++e.get_mut<MaterialInventory>().revision;
		else
			++state.access_revision;
	}
	return true;
}
} // namespace phoenix::world_detail
