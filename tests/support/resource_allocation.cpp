module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Test-only implementation unit of the production module: exercise its private
// allocation seam without exporting simulation internals to ordinary callers.
module phoenix.simulation;
import phoenix.content;
#include "../../src/simulation/gameplay_resources.hpp"

extern "C" void resource_allocation_lifecycle(const phoenix::Catalog &catalog)
{
	using namespace phoenix;
	const auto require = [](bool condition, const char *message)
	{
		if (!condition)
			throw std::runtime_error(message);
	};
	ShipResources ship(*compile_ship_resources(catalog, "test.ship"));
	std::size_t slot = 0;
	while (slot < ship.definition.systems.size() && ship.definition.systems[slot].id != "drive")
		++slot;
	require(slot < ship.definition.systems.size(), "allocation fixture has a fitted drive");
	request_system_resource(ship, slot, 0, false, 200);
	reserve_system_resource_rate(ship, slot, 400);
	reserve_system_resource_rate(ship, slot, 100);
	const auto before = save_ship_resources(ship);
	{
		ShipResourceAllocation discarded(ship, {});
		require(discarded.grants().at(slot).paid == 20 && save_ship_resources(ship) == before,
		        "allocation preview funds minimum reserved demand without mutating live quantities or continuation");
	}
	require(save_ship_resources(ship) == before, "discarded allocation leaves all live resource state unchanged");
	ShipResourceAllocation committed(ship, {}), superseded(ship, {});
	const auto paid = committed.grants().at(slot).paid;
	const auto store = ship.definition.systems.at(slot).store;
	const auto energy = ship.network.stores().at(store).energy();
	committed.commit();
	require(ship.network.ticks() == 1 && ship.network.stores().at(store).energy() == energy - paid,
	        "allocation commit installs the preview's one paid physical tick");
	const auto after = save_ship_resources(ship);
	for (auto *allocation : {&committed, &superseded})
	{
		bool refused = false;
		try
		{
			allocation->commit();
		}
		catch (const std::logic_error &)
		{
			refused = true;
		}
		require(refused && save_ship_resources(ship) == after,
		        "duplicate or superseded allocation commit refuses without changing live resources");
	}
	ShipResourceAllocation stale_stock(ship, {});
	auto &stock = ship.network.stock(store);
	require(stock.add(stock.selected(), 1), "common stock accepts an independent authored charge");
	const auto after_charge = save_ship_resources(ship);
	bool stale_refused = false;
	try
	{
		stale_stock.commit();
	}
	catch (const std::logic_error &)
	{
		stale_refused = true;
	}
	require(stale_refused && save_ship_resources(ship) == after_charge,
	        "prepared electrical allocation cannot overwrite a later common stock mutation");
}
