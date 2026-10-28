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
auto Runtime::process_native_blocker(const CustomPropertyAddress &address, const ProcessState &state,
                                     const ProcessDefinition &definition) const -> std::string
{
	if (definition.native == NativeProcessKind::Effects)
		return {};
	const auto found = entities.find(address.entity);
	const auto ship = found == entities.end() ? flecs::entity{} : found->second;
	NativeMachineryState facts;
	facts.unavailable = !ship || combat_dead(ship) || departed(address.entity);
	facts.dormant = !facts.unavailable && craft_dormant(ship);
	facts.docked = !facts.unavailable && !facts.dormant && definition.native == NativeProcessKind::Propulsion &&
	               dock_attached(address.entity);
	const PersonnelDefinition *personnel = nullptr;
	if (!facts.unavailable && !facts.dormant && definition.native == NativeProcessKind::Recovery)
		personnel = &native_owner_definitions(address.entity).personnel;
	return phoenix::native_machinery_blocker(ship, address, state, definition, personnel, facts);
}

auto Runtime::native_process_view(EntityId entity, std::string_view kind, const std::string &native) const
    -> NativeProcessView
{
	NativeProcessView result;
	for (const auto &address : custom_property_addresses(entity))
		if (const auto *runtime = process_runtime(address))
			consider_native_process(result, address, *runtime, *process_catalog, kind, native);
	return result;
}

auto Runtime::native_process_rate(EntityId entity, std::string_view kind, const std::string &native) const
    -> std::int64_t
{
	if (const auto quoted = process_machinery.rate(entity, kind, native))
		return *quoted;
	const auto view = native_process_view(entity, kind, native);
	return view.state ? view.state->rate_permille : 0;
}

auto Runtime::native_machinery_quote(EntityId entity, std::string_view kind, const std::string &native)
    -> ProcessMachineryQuote *
{
	return process_machinery.find(entity, kind, native);
}

auto Runtime::native_machinery_quote(EntityId entity, std::string_view kind, const std::string &native) const
    -> const ProcessMachineryQuote *
{
	return process_machinery.find(entity, kind, native);
}

auto Runtime::native_process_paid(EntityId entity, std::string_view kind, const std::string &native,
                                  std::int64_t paid) const -> std::int64_t
{
	return process_machinery.paid(entity, kind, native, paid);
}

auto Runtime::native_process_work_limit(EntityId entity, std::string_view kind, const std::string &native) const
    -> std::int64_t
{
	return process_machinery.work_limit(entity, kind, native);
}

auto Runtime::begin_process_machinery_quotes() -> void
{
	process_machinery.begin();
	for (const auto &[entity, ship] : entities)
		for (const auto &address : custom_property_addresses(entity))
			if (const auto *runtime = process_runtime(address))
				process_machinery.add_owner(address, *runtime, *process_catalog);
}

auto Runtime::prepare_process_machinery_controls() -> void
{
	for (const auto &[id, ship] : entities)
		if (auto *power = ship.try_get_mut<ShipResources>())
		{
			power->network.clear_machinery_work();
			for (ResourceIndex i = 0; i < power->network.generators().size(); ++i)
			{
				ResourceMachineryControl control{1000, 0, false};
				const auto &native = power->network.definition().generators[i].id;
				if (auto *quote = native_machinery_quote(id, "generation", native))
				{
					const auto &equipment = quote->owner.equipment;
					const auto scaled = effective_property(ship, equipment, "generation", quote->frozen.rate_permille);
					control = {std::min<std::int64_t>(1000000, scaled), quote->limit, true};
				}
				power->network.set_generator_work(i, control);
			}
			for (ResourceIndex i = 0; i < power->network.cooling_links().size(); ++i)
			{
				ResourceMachineryControl control{1000, 0, false};
				const auto &native = power->network.definition().cooling_links[i].id;
				if (auto *quote = native_machinery_quote(id, "cooling", native))
				{
					const auto scaled =
					    effective_property(ship, quote->owner.equipment, "cooling", quote->frozen.rate_permille);
					control = {std::min<std::int64_t>(1000000, scaled), quote->limit, true};
				}
				power->network.set_cooling_work(i, control);
			}
		}
}

auto Runtime::record_native_process_work(EntityId entity, std::string_view kind, const std::string &native,
                                         std::int64_t available, std::int64_t completed) -> void
{
	process_machinery.record(entity, kind, native, available, completed);
}

auto Runtime::quote_native_flight_work(flecs::entity child, const SystemResourceGrant *grant, bool full) const
    -> std::int64_t
{
	const auto &flight = child.get<FlightRuntime>();
	const auto id = child.parent().get<StableId>().id;
	const auto &equipment = child.get<Equipment>().id;
	FlightWorkFunding funding;
	funding.running = native_process_rate(id, "propulsion", equipment) != 0;
	if (!funding.running)
		return 0;
	funding.available = flight.state.enabled && !craft_dormant(child.parent()) &&
	                    !structurally_disabled(child.parent(), equipment) && !dock_attached(id);
	if (!funding.available)
		return 0;
	funding.full = full;
	auto paid = full ? 1000 : (grant ? grant->supply_permille : 1000);
	funding.paid = full ? paid : native_process_paid(id, "propulsion", equipment, paid);
	funding.work_limit = full ? resource_limit : native_process_work_limit(id, "propulsion", equipment);
	return quote_funded_flight_work(flight.definition, flight.state, flight.demand, grant, funding);
}

auto Runtime::capture_process_native_work(const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
                                          bool stage) -> bool
{
	flight_native->quote_basic_process_work(process_machinery);
	bool changed = false;
	for (auto &quote : process_machinery.rows())
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto &definition = process_definition(quote.frozen);
		const auto allocation = allocations.find(quote.owner.entity);
		const auto *native = allocation != allocations.end() && allocation->second ? &*allocation->second : nullptr;
		std::int64_t available = 0, full = 0;
		if (quote.kind == NativeProcessKind::Effects)
		{
			available = full = quote.frozen.rate_permille;
			if (definition.required_work)
				available = full =
				    std::min(available, std::max<std::int64_t>(0, definition.required_work - quote.frozen.progress));
		}
		else if (quote.kind == NativeProcessKind::Ftl && native)
		{
			const auto fitted = instance(ship, quote.owner.equipment);
			if (fitted && fitted->entity.has<FtlReserve>())
			{
				const auto &ftl = fitted->entity.get<FtlReserve>();
				available = native->prepared_network().telemetry().at(ftl.definition.store).accepted_incoming;
				full = std::max<std::int64_t>(available, ftl.definition.charge_rate / rate);
			}
		}
		else if (quote.kind == NativeProcessKind::Generation && native)
		{
			const auto index = native->prepared_network().definition().generator_ids.at(quote.native);
			const auto &receipt = native->prepared_network().generation_work().at(index);
			available = receipt.completed;
			full = receipt.available;
		}
		else if (quote.kind == NativeProcessKind::Cooling && native)
		{
			const auto index = native->prepared_network().definition().cooling_link_ids.at(quote.native);
			const auto &receipt = native->prepared_network().cooling_work().at(index);
			available = receipt.completed;
			full = receipt.available;
		}
		else if (quote.kind == NativeProcessKind::Processor)
		{
			const auto *processors = ship.try_get<ShipProcessors>();
			if (processors && processors->values.contains(quote.native))
			{
				const auto &processor = processors->values.at(quote.native);
				full = processor.state().prepared_work;
				const auto equipment = instance(ship, processor.definition().equipment);
				const auto paid =
				    native && equipment && equipment->entity.has<ResourceBinding>()
				        ? native->grants().at(equipment->entity.get<ResourceBinding>().slot).supply_permille
				        : 1000;
				available = processor.quoted_funded_work(
				    native_process_paid(quote.owner.entity, "processor", quote.native, paid));
			}
		}
		else if (quote.kind == NativeProcessKind::Propulsion)
		{
			const auto fitted = instance(ship, quote.owner.equipment);
			if (fitted && fitted->entity.has<FlightRuntime>())
			{
				const auto *grant = native && fitted->entity.has<ResourceBinding>()
				                        ? &native->grants().at(fitted->entity.get<ResourceBinding>().slot)
				                        : nullptr;
				available = quote_native_flight_work(fitted->entity, grant);
				full = quote_native_flight_work(fitted->entity, grant, true);
			}
			else if (fitted)
			{
				full = quote.basic_full;
				available = quote.basic_available;
			}
		}
		if (process_machinery.capture(quote, available, full, stage))
		{
			stage_process_work(quote.owner, quote.frozen, definition, quote.work, quote.activity);
			changed = true;
		}
	}
	return changed;
}

auto Runtime::reset_surviving_process_native_caps() -> void
{
	process_machinery.reset_caps(rule_upkeep_plans);
}

auto Runtime::cap_process_native_work(bool producers_only) -> bool
{
	return process_machinery.cap(
	    rule_upkeep_plans,
	    [this](const CustomPropertyAddress &address, const std::string &id) -> const ProcessState *
	    {
		    return process_state(address, id);
	    },
	    producers_only);
}

auto Runtime::capture_process_native_requests() -> void
{
	for (auto &quote : process_machinery.rows())
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto fitted = instance(ship, quote.owner.equipment);
		if (!fitted || !ship.has<ShipResources>() || !fitted->entity.has<ResourceBinding>())
			continue;
		if (quote.kind == NativeProcessKind::Propulsion && fitted->entity.has<FlightRuntime>())
		{
			quote.power_rate = fitted->entity.get<FlightRuntime>().demand.power_rate;
			quote.discrete_request =
			    ship.get<ShipResources>().requests.at(fitted->entity.get<ResourceBinding>().slot).discrete;
		}
		else if (quote.kind == NativeProcessKind::Ftl)
			quote.power_rate =
			    ship.get<ShipResources>().requests.at(fitted->entity.get<ResourceBinding>().slot).extra_rate;
		else if (quote.kind == NativeProcessKind::Processor)
			if (const auto *processors = ship.try_get<ShipProcessors>();
			    processors && processors->energy_rates.contains(quote.native))
				quote.power_rate = processors->energy_rates.at(quote.native);
		quote.applied_power_rate = quote.power_rate;
	}
}

auto Runtime::price_process_native_requests() -> void
{
	for (auto &quote : process_machinery.rows())
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto fitted = instance(ship, quote.owner.equipment);
		auto *power = ship.try_get_mut<ShipResources>();
		if (!fitted || !power || !fitted->entity.has<ResourceBinding>())
			continue;
		auto &request = power->requests.at(fitted->entity.get<ResourceBinding>().slot);
		const auto fraction = native_process_price(quote);
		const auto desired = (quote.power_rate * fraction + 999) / 1000;
		if (request.extra_rate < quote.applied_power_rate)
			throw std::runtime_error("Native process lost its operating request");
		request.extra_rate = request.extra_rate - quote.applied_power_rate + desired;
		quote.applied_power_rate = desired;
		if (quote.kind == NativeProcessKind::Propulsion && quote.discrete_request)
		{
			request.discrete = quote.limit >= 1000;
			if (!request.discrete)
				request.discrete_amount = 0;
		}
		if (quote.kind == NativeProcessKind::Cooling && !fraction)
			request.duty_permille = 0;
		if (quote.kind == NativeProcessKind::Ftl)
		{
			const auto &ftl = fitted->entity.get<FtlReserve>().definition;
			const auto base = ftl.charge_rate * quote.frozen.rate_permille / 1000;
			const auto maximum = quote.limit >= base / rate + 1 ? base : std::min(base, quote.limit * rate);
			power->network.set_store_enabled(ftl.store, maximum > 0);
			power->network.set_storage_limits(ftl.store, maximum,
			                                  power->definition.network.stores.at(ftl.store).discharge_rate);
		}
	}
}
} // namespace phoenix::world_detail
