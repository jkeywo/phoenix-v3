module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay.logistics;

namespace phoenix
{
MaterialSelection select_material_resource(StockStore &stock, MaterialInventory *inventory, EntityId id,
                                           std::vector<MaterialRequest> &requests, const std::string &stock_key,
                                           const std::string &resource, bool execute)
{
	auto candidate = stock;
	if (!candidate.select(resource))
		return MaterialSelection::Rejected;
	if (!execute || stock.selected() == resource)
		return MaterialSelection::Unchanged;
	if (!stock.select(resource))
		return MaterialSelection::Rejected;
	if (inventory)
	{
		for (std::size_t i = 0; i < inventory->stores.size(); ++i)
			if (inventory->stores[i].stock == stock_key)
			{
				std::erase_if(inventory->stores[i].targets,
				              [&](const auto &target)
				              {
					              return target.first != resource;
				              });
				std::erase_if(requests,
				              [&](const auto &request)
				              {
					              return request.material != resource &&
					                     ((request.destination == id && request.to == i) ||
					                      (request.source == id && request.from == i));
				              });
			}
		++inventory->revision;
	}
	return MaterialSelection::Changed;
}
std::string material_batch_claim(std::uint64_t id)
{
	return "batch:" + std::to_string(id);
}
std::string cargo_claim_prefix(std::uint64_t id)
{
	return "cargo:" + std::to_string(id) + ":";
}
bool return_material_batch(MaterialBatch &batch, const MaterialStockAccess &stocks, bool source_accepts, bool execute)
{
	if (batch.returning)
		return true;
	if (batch.location != batch.source || batch.crossing ||
	    (source_accepts ? stocks.at(batch.source, batch.from).headroom(batch.material) : 0) < batch.quantity)
		return false;
	if (execute)
	{
		StockTransaction transaction;
		transaction.release_capacity(stocks.at(batch.destination, batch.to), material_batch_claim(batch.id));
		if (!transaction.reserve_capacity(stocks.at(batch.source, batch.from), material_batch_claim(batch.id),
		                                  batch.material, batch.quantity) ||
		    !transaction.commit())
			return false;
		batch.destination = batch.source;
		batch.to = batch.from;
		batch.returning = true;
	}
	return true;
}
void clear_cargo_claims(StockTransaction &transaction, const CargoDelivery &job, const MaterialStockAccess &stocks)
{
	const auto prefix = cargo_claim_prefix(job.id);
	for (const auto &[entity, index] : std::vector<std::pair<EntityId, std::size_t>>{
	         {job.source, job.from}, {job.carrier, job.cargo}, {job.target, job.to}})
	{
		auto &stock = stocks.at(entity, index);
		const auto snapshot = transaction.view(stock);
		for (const auto &[key, claim] : snapshot.capacity_claims())
			if (key.starts_with(prefix))
				transaction.release_capacity(stock, key);
		for (const auto &[key, claim] : snapshot.stock_claims())
			if (key.starts_with(prefix))
				transaction.release_stock(stock, key);
	}
}
bool stage_cargo_claims(StockTransaction &transaction, const CargoDelivery &job, std::span<const MaterialBatch> batches,
                        const MaterialStockAccess &stocks, const MaterialBatch *dispatch)
{
	const auto prefix = cargo_claim_prefix(job.id);
	if (job.phase == "complete" || job.phase == "lost")
		return true;
	auto capacity =
	    [&](EntityId id, std::size_t store, const std::string &key, const std::string &resource, std::int64_t amount)
	{
		return amount <= 0 || transaction.reserve_capacity(stocks.at(id, store), prefix + key, resource, amount);
	};
	auto claim =
	    [&](EntityId id, std::size_t store, const std::string &key, const std::string &resource, std::int64_t amount)
	{
		return amount <= 0 || transaction.claim_stock(stocks.at(id, store), prefix + key, resource, amount);
	};
	if (!capacity(job.collection ? job.source : job.target, job.collection ? job.from : job.to, "goods-space",
	              job.material, job.reserved))
		return false;
	if (job.collection)
	{
		if (!job.cancelled)
		{
			auto payment_space = job.payment_amount - job.paid;
			for (const auto &batch : batches)
				if (batch.cargo == job.id && batch.destination == job.target && batch.material == job.payment)
					payment_space -= batch.quantity;
			if (dispatch && dispatch->destination == job.target && dispatch->material == job.payment)
				payment_space -= dispatch->quantity;
			if (!capacity(job.target, job.to, "payment-space", job.payment, payment_space) ||
			    !claim(job.target, job.to, "goods", job.material, job.amount - job.goods_taken) ||
			    !claim(job.source, job.from, "payment", job.payment, job.payment_amount - job.payment_taken))
				return false;
		}
		const auto &aboard = transaction.view(stocks.at(job.carrier, job.cargo));
		if (!claim(job.carrier, job.cargo, "aboard-goods", job.material, aboard.quantity(job.material)))
			return false;
		if (!job.payment.empty() && job.payment != job.material &&
		    !claim(job.carrier, job.cargo, "aboard-payment", job.payment, aboard.quantity(job.payment)))
			return false;
	}
	else if (job.phase == "loading" || job.phase == "outbound" || job.phase == "unloading")
	{
		const auto amount =
		    std::min(job.amount, transaction.view(stocks.at(job.carrier, job.cargo)).quantity(job.material));
		if (!claim(job.carrier, job.cargo, "aboard-goods", job.material, amount))
			return false;
	}
	return true;
}
bool stage_material_dispatch(StockTransaction &transaction, const MaterialRequest &request, std::int64_t quantity,
                             std::uint64_t batch_id, std::span<const CargoDelivery> jobs,
                             std::span<const MaterialBatch> batches, const MaterialStockAccess &stocks)
{
	const auto job = std::find_if(jobs.begin(), jobs.end(),
	                              [&](const auto &candidate)
	                              {
		                              return candidate.id == request.cargo;
	                              });
	if (job != jobs.end())
		clear_cargo_claims(transaction, *job, stocks);
	if (!transaction.consume(stocks.at(request.source, request.from), request.material, quantity) ||
	    !transaction.reserve_capacity(stocks.at(request.destination, request.to), material_batch_claim(batch_id),
	                                  request.material, quantity))
		return false;
	if (job != jobs.end())
	{
		auto next = *job;
		if (next.collection)
		{
			if (request.source == next.source && request.material == next.payment)
				next.payment_taken += quantity;
			if (request.source == next.target && request.material == next.material)
				next.goods_taken += quantity;
		}
		if ((!next.collection && request.destination == next.target) ||
		    (next.collection && request.destination == next.source && request.material == next.material))
			next.reserved -= std::min(next.reserved, quantity);
		MaterialBatch batch;
		batch.destination = request.destination;
		batch.material = request.material;
		batch.quantity = quantity;
		if (!stage_cargo_claims(transaction, next, batches, stocks, &batch))
			return false;
	}
	return true;
}
std::int64_t pending_material(const StockStore &stock, EntityId id, std::size_t store, const std::string &m,
                              std::span<const MaterialRequest> requests, std::span<const MaterialBatch> batches,
                              std::span<const CargoDelivery> jobs)
{
	std::int64_t pending = stock.quantity(m);
	for (const auto &[key, claim] : stock.capacity_claims())
		if (key.starts_with("weapon:") && claim.resource == m)
			pending += claim.quantity;
	for (const auto &r : requests)
		if (r.destination == id && r.to == store && r.material == m)
			pending += r.remaining;
	for (const auto &b : batches)
		if (b.destination == id && b.to == store && b.material == m)
			pending += b.quantity;
	for (const auto &job : jobs)
		if (job.collection && job.source == id && job.from == store && job.material == m)
			pending += job.reserved;
		else if (job.target == id && job.to == store && job.material == m)
		{
			auto unrequested = job.reserved;
			for (const auto &request : requests)
				if (request.cargo == job.id && request.destination == id && request.to == store)
					unrequested -= std::min(unrequested, request.remaining);
			pending += unrequested;
		}
	return pending;
}
std::int64_t available_material(const StockStore &stock, const std::string &material, std::uint64_t exempt_cargo)
{
	auto available = stock.available(material);
	if (exempt_cargo)
		for (const auto &[key, claim] : stock.stock_claims())
			if (key.starts_with(cargo_claim_prefix(exempt_cargo)) && claim.resource == material)
				available += claim.quantity;
	return available;
}
bool advance_material_transit(MaterialBatch &batch, const MaterialTransitPorts &ports,
                              MaterialCrossingBudgets &crossing_budgets, std::int64_t ticks_per_second)
{
	if (ports.unavailable(batch.location))
		return false;
	if (batch.crossing)
	{
		const auto gateway = ports.gateway(batch.location, batch.destination, true);
		if (!gateway)
			return false;
		const auto pair = std::minmax(batch.location, batch.destination);
		auto at = crossing_budgets.try_emplace({pair.first, pair.second}, gateway->throughput).first;
		const auto work = std::min(batch.crossing, at->second);
		batch.crossing -= work;
		at->second -= work;
		if (batch.crossing)
			return false;
		batch.location = batch.destination;
		batch.compartment = gateway->to;
	}
	if (batch.travel)
	{
		if (!ports.edge_open(batch.location, batch.compartment, batch.edge_to))
			return false;
		if (--batch.travel)
			return false;
		batch.compartment = batch.edge_to;
		batch.edge_to = -1;
	}
	const auto gateway = batch.location == batch.destination ? std::optional<MaterialGateway>{}
	                                                         : ports.gateway(batch.location, batch.destination, false);
	if (batch.location != batch.destination && !gateway)
		return false;
	const auto destination =
	    batch.location == batch.destination ? ports.store_compartment(batch.destination, batch.to) : gateway->from;
	if (batch.compartment != destination)
	{
		const auto route = ports.route(batch.location, batch.compartment, destination);
		if (route.first < 0)
			return false;
		batch.edge_to = route.first;
		batch.travel = route.second;
		return false;
	}
	if (batch.location != batch.destination)
	{
		batch.crossing = batch.quantity * ticks_per_second;
		return false;
	}
	return true;
}
void record_cargo_delivery(std::span<CargoDelivery> jobs, const MaterialBatch &batch)
{
	for (auto &job : jobs)
		if (job.id == batch.cargo && job.collection && !batch.returning)
		{
			if (batch.destination == job.target && batch.material == job.payment)
				job.paid += batch.quantity;
			if (batch.destination == job.carrier && batch.material == job.material)
				job.collected += batch.quantity;
			if (batch.destination == job.source && batch.material == job.material)
				job.delivered += batch.quantity;
		}
}
} // namespace phoenix
