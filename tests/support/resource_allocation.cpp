module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <limits>
#include <array>
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
#include "../../src/simulation/rule_work.hpp"

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

namespace phoenix
{
void rule_work_contracts();
}

extern "C" void named_resource_allocation_contracts(const phoenix::Catalog &catalog)
{
	phoenix::rule_work_contracts();
	using namespace phoenix;
	const auto require = [](bool condition, const char *message)
	{
		if (!condition)
			throw std::runtime_error(message);
	};
	const auto compiled = *compile_ship_resources(catalog, "test.ship");
	ShipResources ship(compiled);
	std::size_t slot = 0;
	while (slot < ship.definition.systems.size() && ship.definition.systems[slot].id != "drive")
		++slot;
	require(slot < ship.definition.systems.size(), "named allocation fixture has native drive");
	const auto store = ship.definition.systems.at(slot).store;
	request_system_resource(ship, slot, 0, false, 201);
	request_named_resource(ship, "rule:b", store, 103);
	request_named_resource(ship, "rule:a", store, 101);
	const auto before = save_ship_resources(ship);
	{
		ShipResourceAllocation preview(ship, {});
		require(preview.grants().at(slot).work_requested == 201000 && preview.grants().at(slot).work_funded == 9926 &&
		            preview.named_grants().at("rule:a").work_requested == 101000 &&
		            preview.named_grants().at("rule:a").work_funded == 4988 &&
		            preview.named_grants().at("rule:b").work_requested == 103000 &&
		            preview.named_grants().at("rule:b").work_funded == 5086,
		        "shared native and named consumers split high-resolution work with native-first lexical residue");
		require(preview.grants().at(slot).paid + preview.named_grants().at("rule:a").paid +
		                    preview.named_grants().at("rule:b").paid ==
		                1 &&
		            save_ship_resources(ship) == before,
		        "named allocation previews share one physical payment without mutating live resources");
	}
	ShipResourceAllocation paid(ship, {});
	paid.commit();
	require(ship.network.stores().at(store).energy() == 0 && ship.network.stores().at(store).heat == 1 &&
	            ship.network.ticks() == 1,
	        "native and named operations share discharge limits and generate heat once");
	const auto saved = save_ship_resources(ship);
	ShipResources restored(compiled);
	restore_ship_resources(restored, saved);
	require(save_ship_resources(restored) == saved, "named grants restore without advancing or paying again");
	for (const auto &bad : {"duplicate", "funding", "store"})
	{
		auto malformed = saved;
		if (std::string(bad) == "duplicate")
			malformed["named"].push_back(malformed["named"].at(0));
		if (std::string(bad) == "funding")
			malformed["named"][0]["work_funded"] = 100000000;
		if (std::string(bad) == "store")
			malformed["named"][0]["store"] = "missing";
		bool refused = false;
		try
		{
			restore_ship_resources(restored, malformed);
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && save_ship_resources(restored) == saved, "invalid named persistence refuses atomically");
	}
	ShipResourceAllocation original_next(ship, {}), restored_next(restored, {});
	original_next.commit();
	restored_next.commit();
	require(save_ship_resources(ship) == save_ship_resources(restored),
	        "named work funding and thermal continuation match after restore");
	ShipResources withdrawn(compiled);
	request_system_resource(withdrawn, slot, 0, false, 10);
	request_named_resource(withdrawn, "rule:pause", store, 1000);
	{
		ShipResourceAllocation partial(withdrawn, {});
		require(partial.named_grants().at("rule:pause").work_funded <
		            partial.named_grants().at("rule:pause").work_requested,
		        "named ongoing consumer can inspect partial funding before committing");
	}
	clear_named_requests(withdrawn);
	ShipResourceAllocation requoted(withdrawn, {});
	require(requoted.named_grants().empty() && requoted.grants().at(slot).work_funded == 10000,
	        "withdrawing paused demand releases native work in the same prepared allocation");
	requoted.commit();
	require(withdrawn.network.stores().at(store).energy() == 0 && withdrawn.network.stores().at(store).heat == 1,
	        "withdrawn named work never consumes stock or adds heat");
}

namespace phoenix
{
void rule_work_contracts()
{
	const auto require = [](bool condition, const char *message)
	{
		if (!condition)
			throw std::runtime_error(message);
	};
	std::array<RuleWorkInput, 2> inputs{{{100, 0, 100000, 0, true, false}, {200, 0, 0, 1, false, false}}};
	const auto partial = quote_rule_work(inputs, true);
	require(partial.output == 100 && partial.shortage && partial.debits == std::vector<std::int64_t>{0, 1} &&
	            partial.credits == std::vector<std::int64_t>{90000, 0},
	        "mixed bundle output follows its least funded input and retains paid electrical surplus");
	inputs[0].credit = partial.credits[0];
	inputs[0].purchased = 0;
	const auto next = quote_rule_work(inputs, true);
	require(next.output == 100 && next.credits[0] == 80000 && next.debits[0] == 0 && next.debits[1] == 1,
	        "subsequent work consumes existing electrical entitlement without a second electrical charge");
	inputs[0].credit = 7;
	inputs[0].purchased = 100000;
	inputs[1].credit = 9;
	const auto pause = quote_rule_work(inputs, false);
	require(pause.output == 0 && pause.shortage && pause.debits == std::vector<std::int64_t>{0, 0} &&
	            pause.credits == std::vector<std::int64_t>{7, 9},
	        "full-policy shortage preserves only original credits and requires rejecting the electrical quote");
	std::array<RuleWorkInput, 1> small{{{1, 0, 0, 1, false, false}}};
	const auto prepaid = quote_rule_work(small, true);
	require(prepaid.output == 1000 && prepaid.debits[0] == 1 && prepaid.credits[0] == 19000,
	        "fractional material work prepays one whole physical resource before producing output");
	small[0].credit = prepaid.credits[0];
	small[0].available = 0;
	for (int tick = 0; tick < 19; ++tick)
	{
		const auto paid = quote_rule_work(small, true);
		require(paid.output == 1000 && paid.debits[0] == 0, "prepaid work funds exact subsequent ticks");
		small[0].credit = paid.credits[0];
	}
	const auto empty = quote_rule_work(small, true);
	require(small[0].credit == 0 && empty.output == 0 && empty.debits[0] == 0 && empty.shortage,
	        "exhausted prepayment cannot produce an unpaid zero-due pulse");
	small[0] = {1, 0, 0, 0, true, false};
	require(quote_rule_work(small, true).output == 0, "unfunded electrical work never emits a free pulse");
	small[0] = {3, 0, 1999, 0, true, false};
	const auto rounded = quote_rule_work(small, true);
	require(rounded.output == 666 && rounded.credits[0] == 1,
	        "proportional output floors to whole permille and preserves the exact work residue");
	small[0] = {100, 5, 20000, 0, true, true};
	const auto blocked = quote_rule_work(small, true);
	require(blocked.output == 0 && blocked.credits[0] == 20005 && blocked.shortage,
	        "blocked proportional work retains actual payment without executing output");
	small[0] = {0, 5, 0, 0, true, true};
	require(quote_rule_work(small, true).output == 1000 && quote_rule_work({}, false).output == 1000,
	        "zero-rate and cost-free work do not create artificial funding dependencies");
	small[0] = {resource_limit, 0, 0, resource_limit, false, false};
	const auto bounded = quote_rule_work(small, true);
	require(bounded.output == 1000 && bounded.debits[0] == resource_limit / 20 && bounded.credits[0] == 0,
	        "maximum native resource quantities retain exact fixed-point work arithmetic");
	for (const auto malformed :
	     std::array<RuleWorkInput, 4>{{{-1, 0, 0, 0, false, false},
	                                   {1, 0, 1, 1, false, false},
	                                   {1, std::numeric_limits<std::int64_t>::max(), 1, 0, true, false},
	                                   {1, std::numeric_limits<std::int64_t>::max(), 0, resource_limit, false, false}}})
	{
		small[0] = malformed;
		bool refused = false;
		try
		{
			(void)quote_rule_work(small, true);
		}
		catch (const std::runtime_error &)
		{
			refused = true;
		}
		require(refused, "malformed or overflowing work inputs reject before any payment");
	}
}
} // namespace phoenix
