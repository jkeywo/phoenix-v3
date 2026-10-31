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
import phoenix.workshop.authoring_core;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::knowledge(WorkshopDocument &document)
{
	auto &definitions = document.definitions();
	if (!definitions.contains("knowledge"))
	{
		text("Author typed records and permitted producers/readers before binding feeds or publication.");
		if (button("Create knowledge contract"))
			definitions["knowledge"] = workshop_seed("gameplay-editors.knowledge.knowledge");
		return;
	}
	auto &contract = definitions["knowledge"];
	const auto visible_contract = document.preview_knowledge();
	edit_registered(contract, "gameplay-editors.knowledge.history_limit");
	strings(contract, "roles");
	std::vector<std::string> roles;
	if (visible_contract.contains("roles"))
		for (const auto &role : visible_contract.at("roles"))
			roles.push_back(role.get<std::string>());
	records(contract, "schemas", workshop_record("gameplay-editors.knowledge.schemas"),
	        [&](Json &schema)
	        {
		        edit_registered(schema, "gameplay-editors.knowledge.id");
		        names(schema, "producers", roles);
		        names(schema, "readers", roles);
		        edit_registered(schema, "gameplay-editors.knowledge.ship_publishable");
		        edit_registered(schema, "gameplay-editors.knowledge.stale_after_ticks");
		        records(schema, "fields", workshop_record("gameplay-editors.knowledge.fields"),
		                [&](Json &field)
		                {
			                edit_registered(field, "gameplay-editors.knowledge.id");
			                edit_registered(field, "gameplay-editors.knowledge.type");
			                edit_registered(field, "gameplay-editors.knowledge.required");
			                edit_registered(field, "gameplay-editors.knowledge.publishable");
		                });
	        });
	text("Shipwide publication is explicit. Private Console observations and private AI memory are not "
	     "automatically merged. Rendered visibility does not grant knowledge.");
}

void GameplayEditors::State::knowledge_bindings(WorkshopDocument &document)
{
	const auto visible_contract = document.preview_knowledge();
	std::vector<std::string> roles;
	for (const auto &role : visible_contract.at("roles"))
		roles.push_back(role.get<std::string>());
	if (auto *ship = select_entity(document); ship && ship->contains("stations"))
	{
		std::vector<std::string> consoles;
		for (auto &station : (*ship)["stations"])
			for (auto &console : station["consoles"])
			{
				consoles.push_back(console.at("id").get<std::string>());
				ScopedId id_scope_10(console.at("id").get<std::string>().c_str());
				ImGui::TextUnformatted(console.at("id").get<std::string>().c_str());
				names(console, "knowledge_roles", roles);
				names(console, "knowledge_producers", roles);
			}
		const auto schemas = ids(visible_contract.at("schemas"));
		auto inspect_fields = [&](Json &row)
		{
			std::vector<std::string> fields;
			if (visible_contract.contains("schemas"))
				for (const auto &schema : visible_contract.at("schemas"))
					if (schema.at("id") == row.value("schema", std::string{}))
						fields = ids(schema.at("fields"));
			if (fields.empty())
				text("Choose a schema with supported fields first.");
			else
				names(row, "fields", fields);
		};
		records(*ship, "feeds", workshop_record("gameplay-editors.knowledge.feeds"),
		        [&](Json &feed)
		        {
			        choice(feed, "source_console", consoles);
			        choice(feed, "target_console", consoles);
			        choice(feed, "schema", schemas);
			        edit_registered(feed, "gameplay-editors.knowledge.subject");
			        inspect_fields(feed);
		        });
		records(*ship, "publications", workshop_record("gameplay-editors.knowledge.publications"),
		        [&](Json &publication)
		        {
			        edit_registered(publication, "gameplay-editors.knowledge.id");
			        choice(publication, "source_console", consoles);
			        choice(publication, "schema", schemas);
			        edit_registered(publication, "gameplay-editors.knowledge.subject");
			        inspect_fields(publication);
			        edit_registered(publication, "gameplay-editors.knowledge.live");
		        });
		text("Authored feeds refresh only their declared scope. Publication templates require explicit activation; "
		     "defining one does not share it.");
		if (ship->contains("systems"))
			for (auto &system : (*ship)["systems"].items())
			{
				ScopedId identity(system.key().c_str());
				if (ScopedTree inspector(system.key().c_str()); inspector)
					telemetry_records(system.value(), document);
			}
	}
}

void GameplayEditors::State::telemetry_records(Json &spec, WorkshopDocument &document)
{
	const auto knowledge = document.preview_knowledge();
	std::vector<std::string> roles;
	for (const auto &role : knowledge.at("roles"))
		roles.push_back(role.get<std::string>());
	records(spec, "records", workshop_record("gameplay-editors.knowledge.records"),
	        [&](Json &record)
	        {
		        choice(record, "schema", ids(knowledge.at("schemas")));
		        choice(record, "producer", roles);
		        edit_registered(record, "gameplay-editors.knowledge.subject");
		        edit_registered(record, "gameplay-editors.knowledge.kind");
		        std::vector<std::string> fields;
		        for (const auto &schema : knowledge.at("schemas"))
			        if (schema.at("id") == record.value("schema", std::string{}))
				        fields = ids(schema.at("fields"));
		        if (record.contains("fields"))
			        for (auto it = record["fields"].begin(); it != record["fields"].end(); ++it)
			        {
				        ScopedId edit_registered(it.key().c_str());
				        string(record["fields"], it.key().c_str());
			        }
		        static std::map<ImGuiID, std::string> selected_fields;
		        const auto identity = ImGui::GetID("telemetry-field");
		        Json selected{{"Schema field", selected_fields[identity]}};
		        if (choice(selected, "Schema field", fields))
			        selected_fields[identity] = selected.at("Schema field").get<std::string>();
		        if (button("Bind telemetry field") && !selected_fields[identity].empty())
		        {
			        if (!record.contains("fields"))
				        record["fields"] = workshop_seed("gameplay-editors.knowledge.fields");
			        record["fields"][selected_fields[identity]] = "requested";
		        }
		        text("Telemetry source names are verified by the production knowledge compiler. They refer to "
		             "permitted World telemetry, not arbitrary JSON paths.");
		        structured_field(record, "fields");
	        });
}

void GameplayEditors::State::messages(WorkshopDocument &document)
{
	auto &defs = document.definitions();
	const auto known = document.preview_knowledge();
	const auto schemas = ids(known.at("schemas"));
	Json context{{"context", "controller"}};
	std::map<std::string, Json> contexts;
	std::vector<std::string> options;
	for (const auto &id : document.definition_choices(WorkshopDefinitionKind::Tree))
	{
		try
		{
			const auto resolved = document.preview_tree(id);
			if (resolved.value("context", std::string{}) == "controller")
			{
				options.push_back(id);
				contexts.emplace(id, resolved);
			}
		}
		catch (const std::exception &error)
		{
			ImGui::TextWrapped("%s", error.what());
		}
	}
	Json selection{{"Binding tree", message_tree}};
	if (choice(selection, "Binding tree", options, true))
		message_tree = selection.value("Binding tree", std::string{});
	if (contexts.contains(message_tree))
		context = contexts.at(message_tree);
	tree_editor.controller_palette(context, document);
	text("Binding tree supplies authoring choices only. Every actual sender must satisfy the template's bindings.");
	records(defs, "messages", workshop_record("gameplay-editors.knowledge.messages"),
	        [&](Json &message)
	        {
		        edit_registered(message, "gameplay-editors.knowledge.id");
		        choice(message, "schema", schemas);
		        edit_registered(message, "gameplay-editors.knowledge.title_key");
		        edit_registered(message, "gameplay-editors.knowledge.body_key");
		        edit_registered(message, "gameplay-editors.knowledge.urgency");
		        edit_registered(message, "gameplay-editors.knowledge.automatic");
		        edit_registered(message, "gameplay-editors.knowledge.human_seeking");
		        edit_registered(message["destination"], "gameplay-editors.knowledge.kind.2");
		        if (message["destination"].value("kind", std::string{}) == "controller")
			        edit_registered(message["destination"], "gameplay-editors.knowledge.console");
		        edit_registered(message["destination"], "gameplay-editors.knowledge.id");
		        edit_registered(message, "gameplay-editors.knowledge.delay_ticks");
		        edit_registered(message, "gameplay-editors.knowledge.expires_after_ticks");
		        edit_registered(message, "gameplay-editors.knowledge.request_key");
		        std::vector<std::string> fields;
		        if (known.contains("schemas"))
			        for (const auto &schema : known.at("schemas"))
				        if (schema.at("id") == message.value("schema", std::string{}))
					        fields = ids(schema.at("fields"));
		        tree_editor.binding_map(message, "fields", context, fields,
		                                {"integer", "boolean", "text", "argument", "parameter", "observation", "intent",
		                                 "memory", "elapsed"});
		        tree_editor.binding_map(message, "parameters", context, {},
		                                {"integer", "boolean", "text", "argument", "parameter", "observation", "intent",
		                                 "memory", "elapsed"});
	        });

	text("Payload fields are typed data. Display parameters format translated text; AI never parses that text. "
	     "Sending requires a declared action or reporting rule, and delivery does not grant control authority.");
}
} // namespace phoenix
