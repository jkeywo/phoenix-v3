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
void GameplayEditors::State::knowledge(WorkshopDocument &document)
{
	auto &definitions = document.definitions();
	if (!definitions.contains("knowledge"))
	{
		text("Author typed records and permitted producers/readers before binding feeds or publication.");
		if (button("Create knowledge contract"))
			definitions["knowledge"] = {{"history_limit", 256}, {"roles", Json::array()}, {"schemas", Json::array()}};
		return;
	}
	auto &contract = definitions["knowledge"];
	const auto visible_contract = document.preview_knowledge();
	integer(contract, "history_limit", 256);
	strings(contract, "roles");
	std::vector<std::string> roles;
	if (visible_contract.contains("roles"))
		for (const auto &role : visible_contract.at("roles"))
			roles.push_back(role.get<std::string>());
	records(contract, "schemas",
	        {{"id", "record"},
	         {"producers", Json::array()},
	         {"readers", Json::array()},
	         {"ship_publishable", false},
	         {"stale_after_ticks", 100},
	         {"fields", Json::array()}},
	        [&](Json &schema)
	        {
		        string(schema, "id");
		        names(schema, "producers", roles);
		        names(schema, "readers", roles);
		        boolean(schema, "ship_publishable");
		        integer(schema, "stale_after_ticks", 100);
		        records(schema, "fields",
		                {{"id", "field"}, {"type", "integer"}, {"required", true}, {"publishable", true}},
		                [&](Json &field)
		                {
			                string(field, "id");
			                choice(field, "type", {"integer", "boolean", "text"});
			                boolean(field, "required", true);
			                boolean(field, "publishable", true);
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
		records(
		    *ship, "feeds",
		    {{"source_console", ""}, {"target_console", ""}, {"schema", ""}, {"subject", 0}, {"fields", Json::array()}},
		    [&](Json &feed)
		    {
			    choice(feed, "source_console", consoles);
			    choice(feed, "target_console", consoles);
			    choice(feed, "schema", schemas);
			    integer(feed, "subject");
			    inspect_fields(feed);
		    });
		records(*ship, "publications",
		        {{"id", "publication"},
		         {"source_console", ""},
		         {"schema", ""},
		         {"subject", 0},
		         {"fields", Json::array()},
		         {"live", false}},
		        [&](Json &publication)
		        {
			        string(publication, "id");
			        choice(publication, "source_console", consoles);
			        choice(publication, "schema", schemas);
			        integer(publication, "subject");
			        inspect_fields(publication);
			        boolean(publication, "live");
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
	records(spec, "records",
	        {{"schema", ""}, {"producer", ""}, {"subject", 0}, {"kind", "fact"}, {"fields", Json::object()}},
	        [&](Json &record)
	        {
		        choice(record, "schema", ids(knowledge.at("schemas")));
		        choice(record, "producer", roles);
		        integer(record, "subject");
		        choice(record, "kind", {"fact", "claim", "intent", "prediction"});
		        std::vector<std::string> fields;
		        for (const auto &schema : knowledge.at("schemas"))
			        if (schema.at("id") == record.value("schema", std::string{}))
				        fields = ids(schema.at("fields"));
		        if (record.contains("fields"))
			        for (auto it = record["fields"].begin(); it != record["fields"].end(); ++it)
			        {
				        ScopedId field(it.key().c_str());
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
				        record["fields"] = Json::object();
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
	records(defs, "messages",
	        {{"id", "message"},
	         {"schema", ""},
	         {"title_key", ""},
	         {"body_key", ""},
	         {"urgency", 0},
	         {"destination", {{"kind", "ship"}, {"id", ""}}},
	         {"delay_ticks", 1},
	         {"expires_after_ticks", 0},
	         {"request_key", ""},
	         {"fields", Json::object()},
	         {"parameters", Json::object()}},
	        [&](Json &message)
	        {
		        string(message, "id");
		        choice(message, "schema", schemas);
		        string(message, "title_key");
		        string(message, "body_key");
		        integer(message, "urgency");
		        boolean(message, "automatic", true);
		        boolean(message, "human_seeking", false);
		        choice(message["destination"], "kind", {"controller", "console", "station", "ship"});
		        if (message["destination"].value("kind", std::string{}) == "controller")
			        string(message["destination"], "console");
		        string(message["destination"], "id");
		        integer(message, "delay_ticks", 1);
		        integer(message, "expires_after_ticks");
		        string(message, "request_key");
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
