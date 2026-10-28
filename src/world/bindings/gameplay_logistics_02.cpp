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
auto Runtime::restore_logistics(const Json &saved) -> void
{
	material_requests.clear();
	material_batches.clear();
	const auto &j = saved.at("logistics");
	material_requests = j.at("requests").get<std::vector<MaterialRequest>>();
	material_batches = j.at("batches").get<std::vector<MaterialBatch>>();
	cargo_deliveries = j.value("cargo", Json::array()).get<std::vector<CargoDelivery>>();
	next_cargo_delivery = j.value("next_cargo", std::uint64_t{1});
	next_material_request = j.at("next_request");
	next_material_batch = j.at("next_batch");
	if (material_requests.size() > 4096 || material_batches.size() > 4096 || !next_material_request ||
	    !next_material_batch)
		throw std::runtime_error("Invalid logistics continuation size");
	for (const auto &b : saved.at("bodies"))
	{
		const auto id = b.at("body").at("id").get<EntityId>();
		const auto e = entities.at(id);
		if (!e.has<MaterialInventory>())
		{
			if (b.contains("inventory"))
				throw std::runtime_error("Unexpected inventory");
			continue;
		}
		for (const auto &saved_store : b.at("inventory").at("stores"))
			if (!saved_store.is_object() || saved_store.size() != 3 || !saved_store.contains("stock") ||
			    !saved_store.contains("targets") || !saved_store.contains("held"))
				throw std::runtime_error("Invalid inventory store continuation fields");
		const auto state = b.at("inventory").get<MaterialInventory>();
		const auto &d = definition(id).logistics;
		if (state.stores.size() != d.stores.size() || !state.revision || state.revision >= 9007199254740991ULL ||
		    state.handling_remainder < 0 || state.handling_remainder >= rate)
			throw std::runtime_error("Invalid inventory continuation");
		for (std::size_t i = 0; i < state.stores.size(); ++i)
		{
			if (state.stores[i].stock != e.get<MaterialInventory>().stores[i].stock)
				throw std::runtime_error("Invalid inventory stock binding");
			for (const auto &[m, n] : state.stores[i].targets)
				if (!material_accepts(id, i, m) || n < 0 || n > d.stores[i].capacity / material_size(id, i, m))
					throw std::runtime_error("Invalid saved material target");
		}
		if (state.work_credit.size() > d.consumers.size())
			throw std::runtime_error("Invalid saved work credit");
		for (const auto &[key, n] : state.work_credit)
			if (!material_consumer(id, key) || n < 0 || n > rate * 1000)
				throw std::runtime_error("Invalid saved material credit");
		e.set<MaterialInventory>(state);
	}
	auto endpoint = [&](EntityId id, std::size_t i, const std::string &m)
	{
		return entities.contains(id) && entities.at(id).has<MaterialInventory>() &&
		       i < definition(id).logistics.stores.size() && material_accepts(id, i, m);
	};
	std::uint64_t last = 0;
	for (const auto &r : material_requests)
	{
		if (r.id <= last || r.id >= next_material_request || r.remaining <= 0 ||
		    !endpoint(r.source, r.from, r.material) || !endpoint(r.destination, r.to, r.material))
			throw std::runtime_error("Invalid saved supply request");
		if (r.cargo && !r.service.bay.empty())
			throw std::runtime_error("Cargo request cannot also own servicing work");
		validate_craft_service_attribution(r.source, r.destination, r.from, r.to, r.service);
		last = r.id;
	}
	last = 0;
	for (const auto &b : material_batches)
	{
		if (b.id <= last || b.id >= next_material_batch || !b.request || b.request >= next_material_request ||
		    b.quantity <= 0 || !endpoint(b.source, b.from, b.material) || !endpoint(b.destination, b.to, b.material) ||
		    (b.location != b.source && b.location != b.destination) ||
		    b.compartment >= definition(b.location).combat->compartments.size() || b.edge_to < -1 ||
		    b.edge_to >= static_cast<std::int64_t>(definition(b.location).combat->compartments.size()) ||
		    b.crossing < 0 || b.crossing > 1000000000 || b.travel < 0 || (b.travel > 0) != (b.edge_to >= 0))
			throw std::runtime_error("Invalid saved delivery batch");
		if (b.crossing && (b.travel || b.location == b.destination))
			throw std::runtime_error("Invalid crossing location");
		if (b.returning && (b.destination != b.source || b.to != b.from))
			throw std::runtime_error("Invalid returned delivery destination");
		if (b.travel)
		{
			bool edge = false;
			for (const auto &link : definition(b.location).combat->connections)
				edge |= ((link.from == b.compartment && link.to == b.edge_to) ||
				         (link.to == b.compartment && link.from == b.edge_to)) &&
				        b.travel <= link.ticks;
			if (!edge)
				throw std::runtime_error("Invalid saved material route progress");
		}
		if (b.cargo && !b.service.bay.empty())
			throw std::runtime_error("Cargo batch cannot also own servicing work");
		validate_craft_service_attribution(b.source, b.returning ? 0 : b.destination, b.from, b.to, b.service);
		last = b.id;
	}
	validate_cargo();
	// Persisted stock reservations must exactly match native delivery/cargo owners.
	StockTransaction commitments;
	std::set<StockStore *> stores;
	for (const auto &[id, entity] : entities)
		if (auto *physical = entity.try_get_mut<ShipStockStores>())
			for (auto &[key, stock] : physical->stores)
				stores.insert(&stock);
	for (auto *stock : stores)
	{
		for (const auto &[key, claim] : stock->capacity_claims())
		{
			if (key.starts_with("batch:") || key.starts_with("cargo:"))
				commitments.release_capacity(*stock, key);
			else if (!key.starts_with("weapon:") && !key.starts_with("processor:"))
				throw std::runtime_error("Unknown physical receiving commitment owner");
		}
		for (const auto &[key, claim] : stock->stock_claims())
		{
			if (!key.starts_with("cargo:"))
				throw std::runtime_error("Unknown physical stock commitment owner");
			commitments.release_stock(*stock, key);
		}
	}
	for (const auto &batch : material_batches)
		if (!commitments.reserve_capacity(material_stock(batch.destination, batch.to), material_batch_claim(batch.id),
		                                  batch.material, batch.quantity))
			throw std::runtime_error("Invalid saved delivery commitment");
	for (const auto &job : cargo_deliveries)
		if (!stage_cargo_claims(commitments, job))
			throw std::runtime_error("Invalid saved cargo stock commitment");
	for (auto *stock : stores)
		if (stock->capacity_claims() != commitments.view(*stock).capacity_claims() ||
		    stock->stock_claims() != commitments.view(*stock).stock_claims())
			throw std::runtime_error("Orphan or mismatched saved physical commitment");
	for (const auto &[id, e] : inventory_rows())
		for (std::size_t i = 0; i < definition(id).logistics.stores.size(); ++i)
		{
			const auto used = material_occupied_volume(id, i);
			if (used > material_store(id, i).capacity)
				throw std::runtime_error("Saved stock and reservations exceed capacity");
		}
}

auto Runtime::material_consumer(EntityId origin, const std::string &system) const -> const MaterialConsumerDefinition *
{
	for (const auto &c : definition(origin).logistics.consumers)
		if (c.system == system)
			return &c;
	return nullptr;
}

auto Runtime::native_material_work_costs(EntityId origin, const std::string &system, const std::string &activity,
                                         bool paid) const -> std::optional<std::vector<EffectCostDefinition>>
{
	std::vector<EffectCostDefinition> result;
	if (paid)
		return result;
	const auto *consumer = material_consumer(origin, system);
	if (!consumer || !consumer->costs.contains(activity))
		return result;
	const auto &stores = definition(origin).logistics.stores;
	const auto at = std::find_if(stores.begin(), stores.end(),
	                             [&](const auto &store)
	                             {
		                             return store.id == consumer->store;
	                             });
	if (at == stores.end())
		return {};
	if (away_reserved(origin, "team:" + system) && at->system != system)
		return {};
	if (!at->system.empty())
	{
		const auto fitted = instance(entities.at(origin), at->system);
		if (!fitted || !fitted->enabled || !fault_effectiveness(entities.at(origin), at->system))
			return {};
	}
	for (const auto &[resource, amount] : consumer->costs.at(activity))
	{
		EffectCostDefinition cost;
		cost.payer = "owner";
		cost.store = consumer->store;
		cost.resource = resource;
		cost.amount = amount;
		result.push_back(std::move(cost));
	}
	return result;
}

auto Runtime::material_fields(const std::string &text) const -> std::vector<std::string>
{
	std::vector<std::string> out;
	std::size_t begin = 0;
	for (std::size_t i = 0; i <= text.size(); ++i)
		if (i == text.size() || text[i] == '/')
		{
			out.push_back(text.substr(begin, i - begin));
			begin = i + 1;
		}
	return out;
}
} // namespace phoenix::world_detail
