module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdlib>
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
import phoenix.workshop.authoring_core;
import phoenix.content.catalog;
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
			(*ship)["control_layout"] = workshop_seed("gameplay-editors.consoles.control_layout");
		if (!ship->contains("control_layout"))
			return;
	}
	auto &layout = (*ship)["control_layout"];
	if (layout.value("schema", 1) == 2)
	{
		text("Each instance binds a shared widget to equipment. Stars select responsibility levels; data placement is "
		     "independent.");
		records(layout, "controllers", workshop_record("gameplay-editors.consoles.controllers"),
		        [&](Json &instance)
		        {
			        edit_registered(instance, "gameplay-editors.consoles.id");
			        choice(instance, "console", consoles);
			        choice(instance, "widget", document.definition_choices(WorkshopDefinitionKind::ControlWidget));
			        if (instance.value("widget", std::string{}).empty())
				        return;
			        const auto widget = document.preview_definition(WorkshopDefinitionKind::ControlWidget,
			                                                        instance.at("widget").get<std::string>());
			        (void)workshop_custom_action(instance, "controller.roles", {{"definition", widget}});
			        auto &equipment = instance.at("equipment");
			        const auto roles = widget.value("equipment", Json::object());
			        for (const auto &[role, specification] : roles.items())
				        if (specification.value("multiple", false))
					        names(equipment, role.c_str(), systems);
				        else
					        choice(equipment, role.c_str(), systems);
			        auto &levels = instance.at("star_levels");
			        const auto preview = document.preview_ui();
			        const auto widget_id = instance.at("widget").get<std::string>();
			        const auto &data = preview->widget_data.at(widget_id);
			        for (const auto *star : {"1", "2", "3"})
			        {
				        ScopedId scope(star);
				        heading((std::string(star) + " stars").c_str());
				        choice(levels, star, ids(widget.at("levels")));
				        for (const auto &[item, specification] : data.items())
				        {
					        ScopedId data_scope(item.c_str());
					        heading(specification.value("label", item).c_str());
					        edit_shared_control(label("placement").c_str(), instance, "controller.data.placement",
					                            {{"star", star}, {"item", item}});
				        }
			        }
		        });
		records(layout, "profiles", workshop_record("gameplay-editors.consoles.profiles"),
		        [&](Json &profile)
		        {
			        edit_registered(profile, "gameplay-editors.consoles.id");
			        edit_registered(profile, "gameplay-editors.consoles.stars");
		        });
		choice(layout, "default_profile", ids(layout.at("profiles")));
		return;
	}
	text("Controllers own command sections. Profiles choose human or AI operation; absent humans use AI. Remove "
	     "legacy policy/preset bindings before verifying an explicit layout.");
	records(layout, "controllers", workshop_record("gameplay-editors.consoles.controllers.2"),
	        [&](Json &controller)
	        {
		        edit_registered(controller, "gameplay-editors.consoles.id");
		        choice(controller, "console", consoles);
		        edit_registered(controller, "gameplay-editors.consoles.policy");
		        if (tree_editor.tree_binding(document, controller, "tree", "controller", true))
			        (void)workshop_custom_action(controller, "controller.tree");

		        records(controller, "commands", workshop_record("gameplay-editors.consoles.commands"),
		                [&](Json &command)
		                {
			                edit_registered(command, "gameplay-editors.consoles.id");
			                edit_shared_field(label("action").c_str(), command, "controller.command.action");
			                edit_registered(command, "gameplay-editors.consoles.initial");
			                if (const auto *contract = operating_action(command.value("action", std::string{})))
			                {
				                const auto arguments = operating_arguments(contract->action);
				                if (!arguments.empty())
				                {
					                (void)workshop_custom_action(command, "controller.arguments");

					                for (std::size_t i = 0; i < arguments.size(); ++i)
					                {
						                ScopedId scope(std::string(arguments[i].name).c_str());
						                edit_shared_control(label(std::string(arguments[i].name).c_str()).c_str(),
						                                    command, "controller.command.argument", {{"index", i}});
					                }
				                }
				                else
					                (void)workshop_custom_action(command, "controller.arguments");
			                }
			                for (const auto *key : {"minimum", "maximum"})
			                {
				                ScopedId scope(key);
				                if (command.contains(key))
				                {
					                edit_shared_field(label(key).c_str(), command,
					                                  (std::string("controller.command.") + key).c_str());
					                if (button("Use fitted limit"))
						                command.erase(key);
				                }
				                else if (button(key))
					                (void)workshop_custom_action(
					                    command, "control.default",
					                    {{"control", std::string("controller.command.") + key}});
			                }
			                edit_registered(command, "gameplay-editors.consoles.label_key");
			                const auto action = command.value("action", std::string{});
			                if (action == "intent_integer" || action == "intent_boolean" || action == "waypoint")
			                {
				                if (!command.contains("choices"))
				                {
					                if (button("Add labelled choices"))
						                command["choices"] = workshop_seed("gameplay-editors.consoles.choices");
				                }
				                else
				                {
					                records(command, "choices", workshop_record("gameplay-editors.consoles.choices"),
					                        [&](Json &choice)
					                        {
						                        edit_registered(choice, "gameplay-editors.consoles.value");
						                        edit_registered(choice, "gameplay-editors.consoles.label_key");
					                        });
					                if (button("Use continuous values"))
						                command.erase("choices");
				                }
			                }
			                names(command, "consumers", systems);
		                });
	        });
	records(layout, "profiles", workshop_record("gameplay-editors.consoles.profiles.2"),
	        [&](Json &profile)
	        {
		        edit_registered(profile, "gameplay-editors.consoles.id");
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
	edit_registered(console, "gameplay-editors.consoles.human_seeking");
	edit_registered(console, "gameplay-editors.consoles.command");
	edit_registered(console, "gameplay-editors.consoles.hide_without_ai");
	edit_registered(console, "gameplay-editors.consoles.viewscreen");
	optional_string(console, "default_stance");
	records(console, "stances", workshop_record("gameplay-editors.consoles.stances"),
	        [&](Json &stance)
	        {
		        edit_registered(stance, "gameplay-editors.consoles.id");
		        edit_registered(stance, "gameplay-editors.consoles.label");
		        optional_string(stance, "description");
	        });
	if (console.value("viewscreen", false))
	{
		if (!console.contains("compact"))
			(void)workshop_custom_action(console, "console.compact");
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
			{
				edit_registered(widget, "gameplay-editors.consoles.fire_arcs");
				edit_registered(widget, "gameplay-editors.consoles.sensor_arcs");
				strings(widget, "weapon_arcs");
				if (widget.contains("weapon_arcs") && button("Show all weapon arcs"))
					widget.erase("weapon_arcs");
			}
			if (widget.at("kind") == "throttle" || widget.at("kind") == "heading")
			{
				edit_shared_field(label("Use Controller command").c_str(), widget, "widget.controller");
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
				if (std::getenv("PHOENIX_WORKSHOP_PROFILE") && std::string(title) == "Systems and ownership")
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
	ship_graph(document, selection);
	if (tool_window(label("State trees").c_str()))
	{
		try
		{
			tree_editor.database(document, selection);
		}
		catch (const std::exception &error)
		{
			status = error.what();
		}
	}
	ImGui::End();
	if (tool_window(label("State tree editor").c_str()))
		tree_editor.draw(document, false, selection);
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
