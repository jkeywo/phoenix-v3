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
void GameplayEditors::State::control_layout(WorkshopDocument &document)
{
	auto *ship = select_entity(document);
	if (!ship)
		return;
	const auto systems = fitted_ids(document, *ship);
	const auto &resolved = composition.empty() ? *ship : composition;
	std::vector<std::string> consoles;
	for (const auto &station : resolved.value("stations", Json::array()))
		for (const auto &console : station.at("consoles"))
			consoles.push_back(console.at("id").get<std::string>());
	if (consoles.empty() && resolved.contains("console"))
		consoles.push_back("main");
	if (!ship->contains("control_layout"))
	{
		if (resolved.contains("control_layout"))
		{
			text("Controller layout is inherited.");
			if (button("Override Controller layout"))
				(*ship)["control_layout"] = resolved.at("control_layout");
		}
		else if (button("Create Controller layout"))
			(*ship)["control_layout"] = {
			    {"schema", 2}, {"controllers", Json::array()}, {"profiles", Json::array()}, {"default_profile", ""}};
		if (!ship->contains("control_layout"))
			return;
	}
	auto &layout = (*ship)["control_layout"];
	if (layout.value("schema", 1) == 2)
	{
		text("Each instance binds a shared widget to equipment. Stars select responsibility levels; data placement is "
		     "independent.");
		records(layout, "controllers",
		        {{"id", "widget"},
		         {"console", ""},
		         {"widget", ""},
		         {"equipment", Json::object()},
		         {"star_levels", {{"1", "automated"}, {"2", "automated"}, {"3", "automated"}}}},
		        [&](Json &instance)
		        {
			        string(instance, "id");
			        choice(instance, "console", consoles);
			        choice(instance, "widget", document.definition_choices(WorkshopDefinitionKind::ControlWidget));
			        if (instance.value("widget", std::string{}).empty())
				        return;
			        const auto widget = document.preview_definition(WorkshopDefinitionKind::ControlWidget,
			                                                        instance.at("widget").get<std::string>());
			        auto &equipment = instance["equipment"];
			        if (!equipment.is_object())
				        equipment = Json::object();
			        const auto roles = widget.value("equipment", Json::object());
			        for (const auto &[role, specification] : roles.items())
				        if (specification.value("multiple", false))
					        names(equipment, role.c_str(), systems);
				        else
					        choice(equipment, role.c_str(), systems);
			        for (auto it = equipment.begin(); it != equipment.end();)
				        if (!roles.contains(it.key()))
					        it = equipment.erase(it);
				        else
					        ++it;
			        auto &levels = instance["star_levels"];
			        if (!levels.is_object())
				        levels = Json::object();
			        for (const auto *star : {"1", "2", "3"})
			        {
				        ScopedId scope(star);
				        heading((std::string(star) + " stars").c_str());
				        choice(levels, star, ids(widget.at("levels")));
				        const auto data = widget.value("data", Json::object());
				        for (const auto &[item, specification] : data.items())
				        {
					        Json selected{{"placement", instance.value("data_placement", Json::object())
					                                        .value(star, Json::object())
					                                        .value(item, std::string("inherit"))}};
					        ScopedId data_scope(item.c_str());
					        heading(specification.value("label", item).c_str());
					        if (choice(selected, "placement", {"inherit", "main", "details", "hidden"}))
					        {
						        if (selected.at("placement") == "inherit")
						        {
							        if (instance.contains("data_placement") &&
							            instance["data_placement"].contains(star))
								        instance["data_placement"][star].erase(item);
						        }
						        else
							        instance["data_placement"][star][item] = selected.at("placement");
					        }
				        }
			        }
		        });
		records(layout, "profiles", {{"id", "profile"}, {"stars", 1}},
		        [&](Json &profile)
		        {
			        string(profile, "id");
			        integer(profile, "stars", 1);
		        });
		choice(layout, "default_profile", ids(layout.at("profiles")));
		return;
	}
	text("Controllers own command sections. Profiles choose human or AI operation; absent humans use AI. Remove "
	     "legacy policy/preset bindings before verifying an explicit layout.");
	records(
	    layout, "controllers",
	    {{"id", "controller"}, {"console", ""}, {"fallback", "hold"}, {"commands", Json::array()}},
	    [&](Json &controller)
	    {
		    string(controller, "id");
		    choice(controller, "console", consoles);
		    if (choice(controller, "policy", {"combat"}, true) && !controller.value("policy", std::string{}).empty())
		    {
			    controller.erase("tree");
			    controller["fallback"] = "hold";
		    }
		    if (choice(controller, "tree", document.definition_choices(WorkshopDefinitionKind::Tree), true))
		    {
			    if (controller.contains("tree") && !controller.at("tree").get<std::string>().empty())
				    controller.erase("fallback");
			    else
			    {
				    controller.erase("tree");
				    controller["fallback"] = "hold";
			    }
		    }
		    records(controller, "commands",
		            {{"id", "command"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", Json::array()}},
		            [&](Json &command)
		            {
			            string(command, "id");
			            std::vector<std::string> actions;
			            for (const auto &action : operating_actions)
				            actions.emplace_back(action.name);
			            choice(command, "action", actions);
			            integer(command, "initial");
			            if (const auto *contract = operating_action(command.value("action", std::string{})))
			            {
				            const auto arguments = operating_arguments(contract->action);
				            if (!arguments.empty())
				            {
					            if (!command.contains("arguments") || command["arguments"].size() != arguments.size())
					            {
						            command["arguments"] = Json::array();
						            for (const auto &field : arguments)
							            command["arguments"].push_back(std::max<std::int64_t>(0, field.minimum));
					            }
					            for (std::size_t i = 0; i < arguments.size(); ++i)
					            {
						            Json edit{{"value", command["arguments"][i]}};
						            ScopedId scope(std::string(arguments[i].name).c_str());
						            text(std::string(arguments[i].name).c_str());
						            integer(edit, "value");
						            command["arguments"][i] = edit["value"];
					            }
				            }
				            else
					            command.erase("arguments");
			            }
			            for (const auto *key : {"minimum", "maximum"})
			            {
				            ScopedId scope(key);
				            if (command.contains(key))
				            {
					            integer(command, key);
					            if (button("Use fitted limit"))
						            command.erase(key);
				            }
				            else if (button(key))
					            command[key] = 0;
			            }
			            string(command, "label_key");
			            const auto action = command.value("action", std::string{});
			            if (action == "intent_integer" || action == "intent_boolean" || action == "waypoint")
			            {
				            if (!command.contains("choices"))
				            {
					            if (button("Add labelled choices"))
						            command["choices"] = Json::array();
				            }
				            else
				            {
					            records(command, "choices", {{"value", 0}, {"label_key", "Choice"}},
					                    [&](Json &choice)
					                    {
						                    integer(choice, "value");
						                    string(choice, "label_key");
					                    });
					            if (button("Use continuous values"))
						            command.erase("choices");
				            }
			            }
			            names(command, "consumers", systems);
		            });
	    });
	records(layout, "profiles", {{"id", "profile"}, {"consoles", Json::object()}},
	        [&](Json &profile)
	        {
		        string(profile, "id");
		        for (const auto &console : consoles)
		        {
			        ScopedId scope(console.c_str());
			        heading(console.c_str());
			        std::vector<std::string> controllers;
			        for (const auto &controller : layout.at("controllers"))
				        if (controller.value("console", std::string{}) == console)
					        controllers.push_back(controller.at("id").get<std::string>());
			        auto &allocation = profile["consoles"][console];
			        if (!allocation.is_object())
				        allocation = Json::object();
			        names(allocation, "human", controllers);
			        names(allocation, "ai", controllers);
		        }
	        });
	choice(layout, "default_profile", ids(layout.at("profiles")));
}

void GameplayEditors::State::console_properties(WorkshopDocument &document, bool pseudo_language, Json &console)
{
	pseudo = pseudo_language;
	tree_editor.presentation(pseudo, test_client.probing());
	tree_editor.runtime_feedback(test_client.feedback(), test_client.inspected_controller());
	controller_probe = test_client.probing();
	boolean(console, "human_seeking", false);
	boolean(console, "command", false);
	boolean(console, "hide_without_ai", false);
	boolean(console, "viewscreen", false);
	optional_string(console, "default_stance");
	records(console, "stances", {{"id", "stance"}, {"label", "Stance"}},
	        [&](Json &stance)
	        {
		        string(stance, "id");
		        string(stance, "label");
		        optional_string(stance, "description");
	        });
	if (console.value("viewscreen", false))
	{
		if (!console.contains("compact"))
			console["compact"] = {{"widgets", console.value("widgets", Json::array())}};
		ScopedTree compact("Compact Viewscreen HUD");
		if (compact)
			structure(console["compact"], true);
	}
	gameplay_panel(console);
	const auto known = document.preview_knowledge();
	std::vector<std::string> roles;
	for (const auto &role : known.at("roles"))
		roles.push_back(role.get<std::string>());
	names(console, "knowledge_roles", roles);
	names(console, "knowledge_producers", roles);
	std::set<std::string> available;
	std::vector<Json> controller_choices;
	auto collect = [&](const Json &ship)
	{
		bool uses = ship.value("console", std::string{}) == console.at("id").get<std::string>();
		if (ship.contains("stations"))
			for (const auto &station : ship.at("stations"))
				for (const auto &seat : station.at("consoles"))
					uses = uses || seat.value("definition", std::string{}) == console.at("id").get<std::string>();
		if (uses && ship.contains("systems"))
			for (const auto &id : ids(ship.at("systems")))
				available.insert(id);
		if (uses && ship.contains("control_layout"))
			for (const auto &controller : ship.at("control_layout").value("controllers", Json::array()))
				controller_choices.push_back(controller);
	};
	try
	{
		for (const auto &[id, ship] : document.preview_equipment().entities)
			collect(ship);
	}
	catch (const std::exception &)
	{
		for (const auto &ship : document.definitions().at("entities"))
			collect(ship);
	}
	const std::vector<std::string> systems(available.begin(), available.end());
	if (console.contains("widgets"))
		for (auto &widget : console["widgets"])
		{
			ScopedId identity(widget.at("id").get<std::string>().c_str());
			ImGui::TextUnformatted(widget.at("id").get_ref<const std::string &>().c_str());
			if (widget.at("kind") == "radar")
				boolean(widget, "fire_arcs");
			if (widget.at("kind") == "throttle" || widget.at("kind") == "heading")
			{
				bool controlled = widget.contains("controller");
				if (ImGui::Checkbox(label("Use Controller command").c_str(), &controlled))
				{
					if (controlled)
					{
						widget.erase("system");
						widget["controller"] = "";
						widget["channel"] = "";
					}
					else
					{
						widget.erase("controller");
						widget.erase("channel");
						widget["binding"] =
						    widget.at("kind") == "throttle" ? "engine.desired_speed" : "steering.desired_heading";
					}
				}
			}
			if (widget.contains("controller"))
			{
				std::set<std::string> owners, channels;
				for (const auto &controller : controller_choices)
					for (const auto &command : controller.value("commands", Json::array()))
						if (command.value("action", "") ==
						    (widget.at("kind") == "throttle" ? "speed_mm_s" : "heading_mdeg"))
						{
							owners.insert(controller.at("id"));
							if (controller.at("id") == widget.at("controller"))
								channels.insert(command.at("id"));
						}
				choice(widget, "controller", {owners.begin(), owners.end()});
				choice(widget, "channel", {channels.begin(), channels.end()});
			}
			else
				choice(widget, "system", systems, true);
		}
}

void GameplayEditors::State::properties(WorkshopDocument &document, bool pseudo_language, ToolMessage &status,
                                        const std::string &selection, const std::string &selected_entity)
{
	pseudo = pseudo_language;
	tree_editor.presentation(pseudo, test_client.probing());
	tree_editor.runtime_feedback(test_client.feedback(), test_client.inspected_controller());
	controller_probe = test_client.probing();
	entity = selected_entity;
	try
	{
		if (selection == "Entity")
		{
			using Inspector = void (State::*)(WorkshopDocument &);
			for (const auto &[title, inspect] : std::vector<std::pair<const char *, Inspector>>{
			         {"Systems and ownership", &State::fitted},
			         {"Compartments and combat", &State::combat},
			         {"Power and cooling", &State::resources},
			         {"Fitting and assemblies", &State::placements},
			         {"Knowledge feeds and publications", &State::knowledge_bindings},
			         {"Console Controllers and complexity", &State::control_layout}})
			{
				if (controller_probe && std::string(title) == "Console Controllers and complexity")
					ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				if (ImGui::CollapsingHeader(label(title).c_str()))
				{
					ScopedId scope(title);
					(this->*inspect)(document);
				}
			}
		}
		else if (selection == "Templates and assemblies")
			templates(document);
		else if (selection == "Knowledge")
			knowledge(document);
		else if (selection == "Coordination")
			messages(document);
		else if (selection == "State trees")
		{
			auto active = selection;
			tree_editor.draw(document, true, active);
		}
		else if (selection == "Scenario")
			scenario(document);
	}
	catch (const std::exception &error)
	{
		status = error.what();
		ImGui::TextWrapped("%s", status.c_str());
	}
}

void GameplayEditors::State::areas(WorkshopDocument &document, bool pseudo_language, ToolMessage &status,
                                   std::string &selection)
{
	pseudo = pseudo_language;
	tree_editor.presentation(pseudo, test_client.probing());
	tree_editor.runtime_feedback(test_client.feedback(), test_client.inspected_controller());
	controller_probe = test_client.probing();
	if (tool_window(label("State trees").c_str()))
	{
		try
		{
			tree_editor.draw(document, false, selection);
		}
		catch (const std::exception &error)
		{
			status = error.what();
		}
	}
	ImGui::End();
	if (tool_window(label("Scenario").c_str()))
	{
		text("Select a map to inspect its regions, objectives, spawns and situations in Properties.");
		auto &maps = document.definitions()["maps"];
		const auto current = document.map().value("id", std::string{});
		for (std::size_t i = 0; i < maps.size(); ++i)
		{
			const auto id = maps[i].at("id").get<std::string>();
			if (ImGui::Selectable(id.c_str(), selection == "Scenario" && current == id))
			{
				document.select_map(i);
				selection = "Scenario";
			}
		}
		if (button("Inspect scenario"))
			selection = "Scenario";
		if (button("Inspect ship slots"))
			selection = "Ship slots";
	}
	ImGui::End();
	if (controller_probe)
		ImGui::SetNextWindowFocus();
	if (tool_window(label("Test").c_str()))
	{
		try
		{
			test_client.draw(document, status, pseudo);
		}
		catch (const std::exception &error)
		{
			status = error.what();
		}
	}
	ImGui::End();
}
} // namespace phoenix
