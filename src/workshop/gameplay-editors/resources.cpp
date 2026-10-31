module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.gameplay;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::resources(WorkshopDocument &document)
{
	auto *selected = select_entity(document);
	if (!selected)
		return;
	if (!selected->contains("resources"))
	{
		text("This definition has no resource network.");
		if (button("Create resource network"))
			(*selected)["resources"] = Json::object();
		return;
	}
	auto &network = (*selected)["resources"];
	network_editor(network);
}

void GameplayEditors::State::network_editor(Json &network, const std::vector<std::string> &external)
{
	text("Rates are integer units per second. Energy, heat and coolant units are authored. Verification checks "
	     "finite limits and compatible service links.");
	records(network, "groups", workshop_record("gameplay-editors.resources.groups"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        choice(row, "parent",
		               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
		        edit_registered(row, "gameplay-editors.resources.budget_rate");
		        edit_registered(row, "gameplay-editors.resources.restore_margin_rate");
		        edit_registered(row, "gameplay-editors.resources.restore_ticks");
	        });
	records(network, "stores", workshop_record("gameplay-editors.resources.stores"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        edit_registered(row, "gameplay-editors.resources.resource");
		        choice(row, "group",
		               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
		        for (const char *key : {"capacity", "initial", "charge_rate", "discharge_rate", "loss_empty_ppm",
		                                "loss_full_ppm", "initial_heat", "passive_cooling", "stop_heat", "recover_heat",
		                                "charge_heat_ppm", "operation_heat_ppm", "depletion_heat_ppm", "stress_rate",
		                                "override_stress_rate", "stress_limit"})
			        integer(row, key);
		        edit_registered(row, "gameplay-editors.resources.overdrive_permille");
		        edit_registered(row, "gameplay-editors.resources.overdrive_heat_permille");
		        edit_registered(row, "gameplay-editors.resources.enabled");
		        edit_registered(row, "gameplay-editors.resources.allows_override");
	        });
	auto stores = network.contains("stores") ? ids(network.at("stores")) : std::vector<std::string>{};
	stores.insert(stores.end(), external.begin(), external.end());
	records(network, "generators", workshop_record("gameplay-editors.resources.generators"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        choice(row, "store", stores);
		        for (const char *key : {"maximum_rate", "response_rate", "initial_output", "heat_ppm"})
			        integer(row, key);
		        edit_registered(row, "gameplay-editors.resources.enabled");
	        });
	records(network, "links", workshop_record("gameplay-editors.resources.links"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        choice(row, "from", stores);
		        choice(row, "to", stores);
		        edit_registered(row, "gameplay-editors.resources.capacity_rate");
		        edit_registered(row, "gameplay-editors.resources.isolated");
	        });
	records(network, "coolant_sources", workshop_record("gameplay-editors.resources.coolant_sources"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        edit_registered(row, "gameplay-editors.resources.resource");
		        edit_registered(row, "gameplay-editors.resources.capacity");
		        choice(row, "power_store", stores, true);
		        for (const char *key : {"initial", "removal_rate", "coolant_per_heat_ppm"})
			        integer(row, key);
	        });
	auto sources =
	    network.contains("coolant_sources") ? ids(network.at("coolant_sources")) : std::vector<std::string>{};
	sources.insert(sources.end(), external.begin(), external.end());
	records(network, "cooling_links", workshop_record("gameplay-editors.resources.cooling_links"),
	        [&](Json &row)
	        {
		        edit_registered(row, "gameplay-editors.resources.id");
		        choice(row, "source", sources);
		        choice(row, "store", stores);
		        edit_registered(row, "gameplay-editors.resources.capacity_rate");
		        edit_registered(row, "gameplay-editors.resources.allocation_rate");
		        edit_registered(row, "gameplay-editors.resources.isolated");
	        });
}

void GameplayEditors::State::stock_fields(Json &stock)
{
	edit_registered(stock, "gameplay-editors.resources.capacity_tag");
	edit_registered(stock, "gameplay-editors.resources.capacity.2");
	edit_registered(stock, "gameplay-editors.resources.single_resource");
	optional_string(stock, "selection");
	if (!stock.contains("filter"))
		stock["filter"] = Json::object();
	{
		ScopedId scope("stock-filter");
		strings(stock["filter"], "tags");
		strings(stock["filter"], "resources");
		edit_registered(stock["filter"], "gameplay-editors.resources.require_all");
	}
	scalar_map(stock, "initial");
}

void GameplayEditors::State::processor_fields(Json &processor)
{
	optional_string(processor, "recipe");
	strings(processor, "recipes");
	strings(processor, "recipe_tags");
	optional_string(processor, "initial_recipe");
	edit_registered(processor, "gameplay-editors.resources.mode");
	edit_registered(processor, "gameplay-editors.resources.work_per_second");
	edit_registered(processor, "gameplay-editors.resources.energy_per_second");
	scalar_map(processor, "inputs");
	scalar_map(processor, "outputs");
}
} // namespace phoenix
