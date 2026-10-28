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
	records(network, "groups",
	        {{"id", "group"}, {"budget_rate", 1000}, {"restore_margin_rate", 0}, {"restore_ticks", 1}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        choice(row, "parent",
		               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
		        integer(row, "budget_rate");
		        integer(row, "restore_margin_rate");
		        integer(row, "restore_ticks", 1);
	        });
	records(network, "stores",
	        {{"id", "store"},
	         {"capacity", 1000},
	         {"initial", 1000},
	         {"charge_rate", 100},
	         {"discharge_rate", 100},
	         {"stop_heat", 1000},
	         {"recover_heat", 500}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        string(row, "resource");
		        choice(row, "group",
		               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
		        for (const char *key : {"capacity", "initial", "charge_rate", "discharge_rate", "loss_empty_ppm",
		                                "loss_full_ppm", "initial_heat", "passive_cooling", "stop_heat", "recover_heat",
		                                "charge_heat_ppm", "operation_heat_ppm", "depletion_heat_ppm", "stress_rate",
		                                "override_stress_rate", "stress_limit"})
			        integer(row, key);
		        integer(row, "overdrive_permille", 1000);
		        integer(row, "overdrive_heat_permille", 1000);
		        boolean(row, "enabled", true);
		        boolean(row, "allows_override");
	        });
	auto stores = network.contains("stores") ? ids(network.at("stores")) : std::vector<std::string>{};
	stores.insert(stores.end(), external.begin(), external.end());
	records(network, "generators",
	        {{"id", "generator"}, {"store", ""}, {"maximum_rate", 100}, {"response_rate", 100}, {"initial_output", 0}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        choice(row, "store", stores);
		        for (const char *key : {"maximum_rate", "response_rate", "initial_output", "heat_ppm"})
			        integer(row, key);
		        boolean(row, "enabled", true);
	        });
	records(network, "links", {{"id", "link"}, {"from", ""}, {"to", ""}, {"capacity_rate", 100}, {"isolated", false}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        choice(row, "from", stores);
		        choice(row, "to", stores);
		        integer(row, "capacity_rate");
		        boolean(row, "isolated");
	        });
	records(network, "coolant_sources",
	        {{"id", "coolant"}, {"initial", 1000}, {"removal_rate", 100}, {"coolant_per_heat_ppm", 1000000}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        string(row, "resource");
		        integer(row, "capacity", 1000);
		        choice(row, "power_store", stores, true);
		        for (const char *key : {"initial", "removal_rate", "coolant_per_heat_ppm"})
			        integer(row, key);
	        });
	auto sources =
	    network.contains("coolant_sources") ? ids(network.at("coolant_sources")) : std::vector<std::string>{};
	sources.insert(sources.end(), external.begin(), external.end());
	records(network, "cooling_links",
	        {{"id", "cooling"}, {"source", ""}, {"store", ""}, {"capacity_rate", 100}, {"allocation_rate", 100}},
	        [&](Json &row)
	        {
		        string(row, "id");
		        choice(row, "source", sources);
		        choice(row, "store", stores);
		        integer(row, "capacity_rate");
		        integer(row, "allocation_rate");
		        boolean(row, "isolated");
	        });
}

void GameplayEditors::State::stock_fields(Json &stock)
{
	string(stock, "capacity_tag");
	integer(stock, "capacity", 100);
	boolean(stock, "single_resource");
	optional_string(stock, "selection");
	if (!stock.contains("filter"))
		stock["filter"] = Json::object();
	{
		ScopedId scope("stock-filter");
		strings(stock["filter"], "tags");
		strings(stock["filter"], "resources");
		boolean(stock["filter"], "require_all");
	}
	scalar_map(stock, "initial");
}

void GameplayEditors::State::processor_fields(Json &processor)
{
	optional_string(processor, "recipe");
	strings(processor, "recipes");
	strings(processor, "recipe_tags");
	optional_string(processor, "initial_recipe");
	choice(processor, "mode", {"batch", "continuous"});
	integer(processor, "work_per_second", 20);
	integer(processor, "energy_per_second");
	scalar_map(processor, "inputs");
	scalar_map(processor, "outputs");
}
} // namespace phoenix
