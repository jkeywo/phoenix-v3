module;
#include "native_headers.hpp"

module phoenix.simulation;
import :runtime;

namespace phoenix
{
using namespace world_detail;
void World::load(const Catalog &source, const std::string &map, const std::map<std::string, std::string> &ships)
{
	load_impl(source, map, ships, false);
}

void World::load_impl(const Catalog &source, const std::string &map, const std::map<std::string, std::string> &ships,
                      bool restoring)
{
	const auto properties = compile_property_catalog(source);
	const auto content = prepared_catalog(source, properties);
	if (content.presentation)
		throw std::runtime_error("Console presentation assets cannot create an authoritative world");
	if (!content.maps.contains(map))
		throw std::runtime_error("Unknown map: " + map);
	const auto directed_catalog = compile_directed_catalog(content);
	auto candidate = std::make_unique<Impl>();
	candidate->compile(content, directed_catalog, properties);
	candidate->map = map;
	candidate->ships = ships;
	const auto &data = content.maps.at(map);
	candidate->factions = compile_factions(data);
	candidate->combat_duel = data.value("combat_duel", false);
	std::vector<std::string> entity_names;
	for (const auto &[name, definition] : content.entities)
		entity_names.push_back(name);
	std::vector<std::string> slot_names;
	for (const auto &slot : data.at("ship_slots"))
		slot_names.push_back(slot.at("id"));
	candidate->regions = compile_scenario(data, entity_names, slot_names).regions;
	candidate->region_rule_bindings = compile_region_rules(content, data, candidate->rule_templates);
	candidate->validate_native_payload_layouts();
	candidate->field_seed = asteroid_map_seed(map);
	EntityId next = 1;
	for (const auto &instance : data.at("entities"))
	{
		EntityId id = instance.at("id");
		if (id >= Impl::field_identity_base)
			throw std::runtime_error("Authored identity overlaps reserved asteroid range");
		candidate->create(id, instance.at("definition"), instance);
		next = std::max(next, id + 1);
	}
	std::map<std::string, Json> slots;
	for (const auto &slot : data.at("ship_slots"))
		slots[slot.at("id").get<std::string>()] = slot;
	for (const auto &[slot, ship] : ships)
	{
		if (!slots.contains(slot))
			throw std::runtime_error("Unknown ship slot: " + slot);
		const auto &allowed = slots.at(slot).at("allowed_ships");
		if (std::find(allowed.begin(), allowed.end(), Json(ship)) == allowed.end())
			throw std::runtime_error("Ship is not allowed in slot: " + slot);
		candidate->slot_entities[slot] = next;
		candidate->create(next++, ship, slots.at(slot));
	}
	for (const auto &setup : compile_starting_stocks(content, data, directed_catalog, candidate->property_catalogue))
		if (ships.contains(setup.slot) && ships.at(setup.slot) == setup.definition)
		{
			const auto id = candidate->slot_entities.at(setup.slot);
			const auto &stores = candidate->definition(id).logistics.stores;
			for (const auto &[name, quantities] : setup.stores)
			{
				const auto found = std::find_if(stores.begin(), stores.end(),
				                                [&](const auto &store)
				                                {
					                                return store.id == name;
				                                });
				auto &stock = candidate->material_stock(id, std::distance(stores.begin(), found));
				// Remove replaced quantities first, so changing cargo types cannot overfill mid-setup.
				for (const auto &[resource, quantity] : quantities)
					if (const auto existing = stock.quantity(resource); existing && !stock.consume(resource, existing))
						throw std::runtime_error("Starting stock is already committed");
				for (const auto &[resource, quantity] : quantities)
					if (quantity && !stock.add(resource, quantity))
						throw std::runtime_error("Starting stock exceeds the receiving store");
			}
		}
	candidate->initialise_craft();
	if (content.scenarios.contains(map))
	{
		candidate->scenario.emplace();
		candidate->scenario->load(content, map, candidate->property_catalogue);
	}
	candidate->conversations.programs = content.conversation_programs;
	candidate->compile_conversation_starts(data);
	candidate->away_operations.configure_definitions(
	    compile_away(content, data, directed_catalog, candidate->property_catalogue));
	candidate->interdiction_book.configure(
	    compile_interdictions(content, data, directed_catalog, candidate->property_catalogue));
	candidate->sensor_findings.configure(compile_sensor_findings(content, data));
	if (candidate->scenario && !candidate->sensor_findings.empty() && !candidate->conversation_instance)
		candidate->conversation_instance = candidate->scenario->runtime().create_instance("sensor-events");
	if (candidate->scenario && !candidate->conversation_instance &&
	    (!candidate->scenario->runtime().definition().fact_bindings.empty() ||
	     !candidate->scenario->runtime().definition().event_bindings.empty() ||
	     !candidate->scenario->runtime().definition().reports.empty()))
		candidate->conversation_instance = candidate->scenario->runtime().create_instance("world-events");
	candidate->away_operations.initialize_missions();
	candidate->refresh_boarding_templates();
	if (!restoring)
	{
		candidate->initialise_rule_membership();
		candidate->prepare_rule_sources();
	}
	candidate->refresh_comms();
	candidate->update_craft_reports();
	candidate->stream_fields();
	candidate->index_contacts.run();
	// Instant capabilities are observations even before the first simulation step.
	// Timed acquisition must not gain exposure while constructing the world.
	candidate->advance_sensors(false);
	impl_ = std::move(candidate);
}
} // namespace phoenix
