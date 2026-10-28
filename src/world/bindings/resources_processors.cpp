module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::processor_stores(flecs::entity ship, const ProcessorDefinition &definition) const -> ProcessorStores
{
	ProcessorStores stores;
	for (const auto *bindings : {&definition.inputs, &definition.outputs})
		for (const auto &[resource, name] : *bindings)
			stores.emplace(name, &named_stock(ship, name).stock);
	return stores;
}

auto Runtime::initialize_processors(flecs::entity ship, const CompiledDefinition &data) -> void
{
	ShipProcessors processors;
	for (const auto &definition : data.processors)
		if (!processors.values.emplace(definition.id, ResourceProcessor(definition)).second)
			throw std::runtime_error("Duplicate processor identity");
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *mechanism = child.try_get<CombatMechanism>();
		    mechanism && mechanism->definition.kind == OperatingSystem::Magazine)
			if (const auto &definition = mechanism->definition.get<MagazineDefinition>().processor)
				if (!processors.values.emplace(definition->id, ResourceProcessor(*definition)).second)
					throw std::runtime_error("Duplicate magazine processor identity");
	for (const auto &[id, processor] : processors.values)
	{
		const auto bindings = processor_stores(ship, processor.definition());
		for (const auto &recipe : processor.definition().recipes)
		{
			for (const auto &[resource, amount] : recipe.inputs)
				if (!bindings.at(processor.definition().inputs.at(resource))->allows(resource))
					throw std::runtime_error("Processor input store rejects resource");
			for (const auto &[resource, amount] : recipe.outputs)
				if (!bindings.at(processor.definition().outputs.at(resource))->allows(resource))
					throw std::runtime_error("Processor output store rejects resource");
		}
	}
	ship.set<ShipProcessors>(std::move(processors));
}

auto Runtime::prepare_processors(flecs::entity ship) -> void
{
	if (!ship.has<ShipProcessors>())
		return;
	auto &processors = ship.get_mut<ShipProcessors>();
	processors.energy_rates.clear();
	processors.work_quotes.clear();
	for (auto &[id, processor] : processors.values)
	{
		const auto &definition = processor.definition();
		const auto equipment = instance(ship, definition.equipment);
		if (!equipment || !equipment->enabled || structurally_disabled(ship, definition.equipment) || combat_dead(ship))
			continue;
		const bool magazine_reload =
		    equipment->kind == OperatingSystem::Magazine &&
		    equipment->entity.get<CombatMechanism>().definition.get<MagazineDefinition>().processor &&
		    equipment->entity.get<CombatMechanism>().definition.get<MagazineDefinition>().processor->id == id;
		if (magazine_reload && duel_ended())
			continue;
		const auto process_rate = native_process_rate(ship.get<StableId>().id, "processor", id);
		if (!process_rate)
			continue;
		const auto property = magazine_reload ? PropertyKey::reload : PropertyKey::processing;
		auto milli_rate = apply_property_delta(definition.work_per_second * 1000,
		                                       property_delta(ship, definition.equipment, property) +
		                                           property_fault_delta(ship, definition.equipment, property),
		                                       resource_limit);
		// The process rate changes this tick's work quotation, never the native
		// recipe definition or accumulated batch/receiving commitments.
		milli_rate = apply_property_delta(milli_rate, process_rate - 1000, resource_limit);
		const auto work = processor.milli_work_budget(milli_rate);
		processors.work_quotes[id] = work;
		if (work && processor.prepare(processor_stores(ship, definition), work))
			processors.energy_rates[id] = processor.operating_rate(work);
	}
}

auto Runtime::request_processor_resources(flecs::entity ship) -> void
{
	const auto *processors = ship.try_get<ShipProcessors>();
	auto *power = ship.try_get_mut<ShipResources>();
	if (!processors || !power)
		return;
	for (const auto &[id, energy] : processors->energy_rates)
	{
		if (!energy)
			continue;
		const auto &definition = processors->values.at(id).definition();
		const auto equipment = instance(ship, definition.equipment);
		const auto slot = equipment->entity.get<ResourceBinding>().slot;
		const auto request = power->requests.at(slot);
		if (energy > resource_limit - request.extra_rate)
			throw std::runtime_error("Combined processor operating rate exceeds supported bound");
		request_system_resource(*power, slot, request.duty_permille, request.discrete, request.extra_rate + energy,
		                        request.discrete_amount);
	}
}

auto Runtime::retry_blocked_processors(flecs::entity ship) -> void
{
	if (!ship.has<ShipProcessors>())
		return;
	auto &processors = ship.get_mut<ShipProcessors>();
	for (const auto &[id, work] : processors.work_quotes)
	{
		auto &processor = processors.values.at(id);
		if (!work || processor.state().prepared_work ||
		    !processor.prepare(processor_stores(ship, processor.definition()), work))
			continue;
		const auto energy = processor.operating_rate(work);
		processors.energy_rates[id] = energy;
		// This processor was blocked when the tick's native requests were
		// captured. Its newly prepared work now owns this request component.
		if (auto *quote = native_machinery_quote(ship.get<StableId>().id, "processor", id))
			quote->power_rate = quote->applied_power_rate = energy;
		if (auto *power = ship.try_get_mut<ShipResources>(); power && energy)
		{
			const auto equipment = instance(ship, processor.definition().equipment)->entity;
			const auto slot = equipment.get<ResourceBinding>().slot;
			const auto request = power->requests.at(slot);
			if (energy > resource_limit - request.extra_rate)
				throw std::runtime_error("Combined processor operating rate exceeds supported bound");
			request_system_resource(*power, slot, request.duty_permille, request.discrete, request.extra_rate + energy,
			                        request.discrete_amount);
		}
	}
}

auto Runtime::actuate_processors(flecs::entity ship) -> void
{
	if (!ship.has<ShipProcessors>())
		return;
	auto &processors = ship.get_mut<ShipProcessors>();
	for (auto &[id, processor] : processors.values)
	{
		if (!processor.state().prepared_work)
			continue;
		const auto &definition = processor.definition();
		const auto equipment = instance(ship, definition.equipment);
		std::int64_t supply = 1000;
		if (!equipment || !equipment->enabled || structurally_disabled(ship, definition.equipment) || combat_dead(ship))
			supply = 0;
		else if (definition.energy_per_second)
		{
			supply = 0;
			if (const auto *power = ship.try_get<ShipResources>())
			{
				const auto &grant = power->grants.at(equipment->entity.get<ResourceBinding>().slot);
				if (!grant.blocked)
					supply = grant.supply_permille;
			}
		}
		const auto available = processor.quoted_funded_work(supply);
		const auto funded = native_process_paid(ship.get<StableId>().id, "processor", id, supply);
		const auto completed =
		    processor.funded_work(funded, native_process_work_limit(ship.get<StableId>().id, "processor", id));
		processor.advance(processor_stores(ship, definition), completed);
		record_native_process_work(ship.get<StableId>().id, "processor", id, available, completed);
	}
	processors.energy_rates.clear();
	processors.work_quotes.clear();
}

auto Runtime::select_processor(flecs::entity ship, const std::string &id, const std::string &recipe, bool execute)
    -> bool
{
	auto *processors = ship.try_get_mut<ShipProcessors>();
	if (!processors || !processors->values.contains(id))
		return false;
	auto &processor = processors->values.at(id);
	if (!execute)
		return processor.can_select(processor_stores(ship, processor.definition()), recipe);
	return processor.select(processor_stores(ship, processor.definition()), recipe);
}

auto Runtime::save_processors(flecs::entity ship) const -> Json
{
	Json result = Json::object();
	for (const auto &[id, processor] : ship.get<ShipProcessors>().values)
		result[id] = processor.save();
	return result;
}

auto Runtime::restore_processors(flecs::entity ship, const Json &saved) -> void
{
	auto &processors = ship.get_mut<ShipProcessors>();
	if (!saved.is_object() || saved.size() != processors.values.size())
		throw std::runtime_error("Saved processor roster mismatch");
	for (auto &[id, processor] : processors.values)
		processor.restore(saved.at(id), processor_stores(ship, processor.definition()));
	processors.energy_rates.clear();
	processors.work_quotes.clear();
}

auto Runtime::project_processors(const ConsoleAddress &address) const -> Json
{
	Json rows = Json::array();
	const auto ship = entities.at(address.ship);
	const auto *processors = ship.try_get<ShipProcessors>();
	if (!processors)
		return rows;
	const auto &feeds = seat(address.ship, address.console)->feed_systems;
	for (const auto &[id, processor] : processors->values)
	{
		const auto &definition = processor.definition();
		if (std::find(feeds.begin(), feeds.end(), definition.equipment) == feeds.end())
			continue;
		Json choices = Json::array();
		std::int64_t work = 0;
		for (const auto &recipe : definition.recipes)
		{
			choices.push_back({{"id", recipe.id},
			                   {"label", recipe.label},
			                   {"selectable", processor.can_select(processor_stores(ship, definition), recipe.id)}});
			if (recipe.id == processor.state().recipe)
				work = recipe.work * 1000;
		}
		rows.push_back({{"id", id},
		                {"equipment", definition.equipment},
		                {"recipe", processor.state().recipe},
		                {"mode", definition.continuous ? "continuous" : "batch"},
		                {"active", processor.state().active},
		                {"progress", processor.state().progress},
		                {"work", work},
		                {"recipes", choices},
		                {"manual", manual_workload(address, "logistics")},
		                {"selection_revision", processor.state().selection_revision}});
	}
	return rows;
}
} // namespace phoenix::world_detail
