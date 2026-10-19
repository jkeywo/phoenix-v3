module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.content;
namespace phoenix
{
namespace
{
void knowledge_require(bool condition, const std::string &reason)
{
	if (!condition)
		throw std::runtime_error("gameplay knowledge: " + reason);
}
KnowledgeId knowledge_unsigned(const Json &value)
{
	knowledge_require(value.is_number_integer(), "expected a nonnegative integral identity/tick");
	if (value.is_number_unsigned())
		return value.get<KnowledgeId>();
	const auto number = value.get<std::int64_t>();
	knowledge_require(number >= 0, "negative identity/tick");
	return static_cast<KnowledgeId>(number);
}
bool knowledge_has(const std::vector<KnowledgeKey> &keys, KnowledgeKey key)
{
	return std::find(keys.begin(), keys.end(), key) != keys.end();
}
const KnowledgeSchema &knowledge_schema(const CompiledKnowledge &definition, KnowledgeKey key)
{
	knowledge_require(key > 0 && key <= definition.schemas.size(), "unknown schema key");
	return definition.schemas[key - 1];
}
KnowledgeValue knowledge_scalar(const Json &value, KnowledgeType type)
{
	if (type == KnowledgeType::Boolean && value.is_boolean())
		return value.get<bool>();
	if (type == KnowledgeType::Text && value.is_string())
		return value.get<std::string>();
	if (type == KnowledgeType::Integer && value.is_number_integer())
	{
		if (value.is_number_unsigned())
			knowledge_require(value.get<std::uint64_t>() <=
			                      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
			                  "integer field overflow");
		return value.get<std::int64_t>();
	}
	throw std::runtime_error("gameplay knowledge: literal field has wrong schema type");
}
std::vector<KnowledgeKey> knowledge_roles(const CompiledKnowledge &compiled, const Json &config, const char *name,
                                          const std::vector<KnowledgeKey> &fallback = {})
{
	if (!config.contains(name))
		return fallback;
	knowledge_require(config.at(name).is_array(), std::string(name) + " requires an array");
	std::vector<KnowledgeKey> result;
	for (const auto &role : config.at(name))
	{
		const auto key = compiled.role(role.get<std::string>());
		knowledge_require(!knowledge_has(result, key), "duplicate Console role");
		result.push_back(key);
	}
	return result;
}
MessageDestinationKind knowledge_destination(const std::string &name)
{
	if (name == "system")
		return MessageDestinationKind::System;
	if (name == "console")
		return MessageDestinationKind::Console;
	if (name == "station")
		return MessageDestinationKind::Station;
	if (name == "ship")
		return MessageDestinationKind::Ship;
	throw std::runtime_error("gameplay knowledge: unsupported message destination '" + name + "'");
}
bool knowledge_readable(const CompiledKnowledge &compiled, const GameplayKnowledgeConsoleDefinition &console,
                        KnowledgeKey key)
{
	const auto &schema = knowledge_schema(compiled, key);
	for (const auto role : console.readers)
		if (knowledge_has(schema.readers, role))
			return true;
	return false;
}
bool knowledge_telemetry_source(const std::string &name)
{
	// This is the production World telemetry vocabulary, not arbitrary JSON paths.
	static const std::set<std::string> supported{"enabled",
	                                             "requested",
	                                             "desired_speed",
	                                             "desired_heading",
	                                             "minimum_speed",
	                                             "assisted",
	                                             "longitudinal_thrust",
	                                             "lateral_thrust",
	                                             "manual_steering",
	                                             "boost",
	                                             "impulse",
	                                             "impulse_phase",
	                                             "impulse_progress",
	                                             "match_speed",
	                                             "selected_contact",
	                                             "match_stale",
	                                             "matched_speed",
	                                             "actual_longitudinal",
	                                             "actual_lateral",
	                                             "actual_steering",
	                                             "supply_permille",
	                                             "used_power",
	                                             "reserved_power",
	                                             "supply_limited",
	                                             "stored_energy",
	                                             "energy_capacity",
	                                             "incoming_power",
	                                             "heat",
	                                             "heat_capacity",
	                                             "stress",
	                                             "thermal_stopped",
	                                             "thermal_override",
	                                             "overdrive",
	                                             "charge_limit",
	                                             "discharge_limit",
	                                             "passive_cooling",
	                                             "actual_cooling",
	                                             "depletion_ticks",
	                                             "thermal_stop_ticks",
	                                             "generator_output",
	                                             "actual_output",
	                                             "group_allocation",
	                                             "flow_cut",
	                                             "link_isolated",
	                                             "cooling_allocation",
	                                             "coolant_stock"};
	return supported.contains(name);
}
void knowledge_dynamic_binding(const Json &value, const std::string &path, std::optional<KnowledgeType> type = {})
{
	knowledge_require(value.is_object() && value.size() == 1,
	                  path + ": a dynamic binding requires one supported source");
	const auto source = value.begin().key();
	const auto &binding = value.begin().value();
	if (source == "elapsed")
	{
		knowledge_require(binding.is_boolean() && binding.get<bool>() && (!type || *type == KnowledgeType::Integer),
		                  path + ": invalid elapsed binding");
		return;
	}
	knowledge_require(source == "argument" || source == "parameter" || source == "observation" || source == "intent" ||
	                      source == "memory",
	                  path + ": unsupported dynamic binding source");
	knowledge_require(binding.is_string() && !binding.get<std::string>().empty(),
	                  path + ": binding name must be nonempty text");
	if (source == "argument")
	{
		const auto name = binding.get<std::string>();
		knowledge_require(name == "amount" || name == "flag" || name == "detail",
		                  path + ": unsupported message argument");
		if (type)
			knowledge_require((name == "amount" && *type == KnowledgeType::Integer) ||
			                      (name == "flag" && *type == KnowledgeType::Boolean) ||
			                      (name == "detail" && *type == KnowledgeType::Text),
			                  path + ": message argument type differs from field");
	}
	if (source == "observation" || source == "intent")
	{
		const auto registry = gameplay_tree_registry(state_tree::Context::System);
		const auto &fields = source == "observation" ? registry.observations : registry.intents;
		const auto found = std::find_if(fields.begin(), fields.end(),
		                                [&](const auto &field)
		                                {
			                                return field.name == binding.get<std::string>();
		                                });
		knowledge_require(found != fields.end(), path + ": unknown supported " + source + " binding");
		if (type)
			knowledge_require((*type == KnowledgeType::Integer && found->type == state_tree::Type::Integer) ||
			                      (*type == KnowledgeType::Boolean && found->type == state_tree::Type::Boolean) ||
			                      (*type == KnowledgeType::Text && found->type == state_tree::Type::Text),
			                  path + ": binding source type differs from field");
	}
}
} // namespace
GameplayKnowledgeDefinitions compile_gameplay_knowledge(const Catalog &catalog)
{
	GameplayKnowledgeDefinitions compiled;
	compiled.knowledge = catalog.knowledge_definition;
	// Restricted crew presentation bundles deliberately carry no producer/schema
	// authoring graph. Their ordinary UI verification runs independently.
	if (catalog.presentation)
		return compiled;

	auto compile_record = [&](const Json &row, const std::string &path)
	{
		GameplayKnowledgeShipDefinition::TelemetryRecord record;
		record.schema = compiled.knowledge.schema(row.at("schema").get<std::string>());
		record.producer = compiled.knowledge.role(row.at("producer").get<std::string>());
		record.subject = row.contains("subject") ? knowledge_unsigned(row.at("subject")) : 0;
		const auto &schema = knowledge_schema(compiled.knowledge, record.schema);
		knowledge_require(knowledge_has(schema.producers, record.producer),
		                  path + ": telemetry producer cannot write schema");
		const auto kind = row.value("kind", std::string("fact"));
		knowledge_require(kind == "fact" || kind == "claim" || kind == "intent" || kind == "prediction",
		                  path + ": unsupported telemetry evidence kind");
		record.kind = kind == "claim"        ? EvidenceKind::Claim
		              : kind == "intent"     ? EvidenceKind::Intent
		              : kind == "prediction" ? EvidenceKind::Prediction
		                                     : EvidenceKind::Fact;
		knowledge_require(row.at("fields").is_object(), path + ": telemetry fields must be named bindings");
		std::set<KnowledgeKey> fields;
		for (auto field = row.at("fields").begin(); field != row.at("fields").end(); ++field)
		{
			const auto key = compiled.knowledge.field(record.schema, field.key());
			const auto value = field.value().get<std::string>();
			knowledge_require(knowledge_telemetry_source(value),
			                  path + ": unknown supported telemetry source '" + value + "'");
			knowledge_require(schema.fields.at(key - 1).type != KnowledgeType::Text,
			                  path + ": numeric/Boolean World telemetry cannot populate a text field");
			if (!compiled.telemetry_indices.contains(value))
			{
				compiled.telemetry_indices.emplace(value, compiled.telemetry_names.size());
				compiled.telemetry_names.push_back(value);
			}
			record.fields.push_back({key, compiled.telemetry_indices.at(value), schema.fields.at(key - 1).type,
			                         schema.fields.at(key - 1).required});
			fields.insert(key);
		}
		for (const auto &field : schema.fields)
			knowledge_require(!field.required || fields.contains(field.key),
			                  path + ": required telemetry schema field is unbound");
		return record;
	};
	for (const auto &[name, console] : catalog.consoles)
	{
		knowledge_roles(compiled.knowledge, console, "knowledge_roles");
		knowledge_roles(compiled.knowledge, console, "knowledge_producers");
	}
	for (const auto &[name, equipment] : catalog.equipment)
		for (const auto &row : equipment.value("records", Json::array()))
			(void)compile_record(row, "equipment." + name);
	for (const auto &[name, assembly] : catalog.assemblies)
		if (assembly.contains("systems"))
			for (auto system = assembly.at("systems").begin(); system != assembly.at("systems").end(); ++system)
				for (const auto &row : system.value().value("records", Json::array()))
					(void)compile_record(row, "assemblies." + name + ".systems." + system.key());
	for (const auto &[name, entity] : catalog.entities)
	{
		if (!entity.contains("systems"))
			continue;
		GameplayKnowledgeShipDefinition definition;
		for (const auto &console : resolve_consoles(catalog, name))
		{
			const Json *config = &entity;
			if (entity.contains("stations"))
				for (const auto &station : entity.at("stations"))
					for (const auto &candidate : station.at("consoles"))
						if (candidate.at("id") == console.id)
							config = &candidate;
			std::vector<KnowledgeKey> defaults;
			if (catalog.consoles.contains(console.definition))
				defaults =
				    knowledge_roles(compiled.knowledge, catalog.consoles.at(console.definition), "knowledge_roles");
			GameplayKnowledgeConsoleDefinition target{
			    console, knowledge_roles(compiled.knowledge, *config, "knowledge_roles", defaults), {}};
			if (std::find(compiled.knowledge.roles.begin(), compiled.knowledge.roles.end(), "phoenix.crew") !=
			    compiled.knowledge.roles.end())
			{
				const auto crew = compiled.knowledge.role("phoenix.crew");
				if (!knowledge_has(target.readers, crew))
					target.readers.push_back(crew);
			}
			auto producer_defaults = target.readers;
			if (catalog.consoles.contains(console.definition))
				producer_defaults = knowledge_roles(compiled.knowledge, catalog.consoles.at(console.definition),
				                                    "knowledge_producers", producer_defaults);
			target.producers = knowledge_roles(compiled.knowledge, *config, "knowledge_producers", producer_defaults);
			for (const auto &system : console.controlled_systems)
				knowledge_require(definition.owners.emplace(system, console.id).second,
				                  name + ": duplicate controlling Console");
			definition.consoles.push_back(std::move(target));
		}
		for (auto system = entity.at("systems").begin(); system != entity.at("systems").end(); ++system)
			for (const auto &row : system.value().value("records", Json::array()))
			{
				auto record = compile_record(row, "entities." + name + ".systems." + system.key());
				knowledge_require(definition.owners.contains(system.key()),
				                  name + ": telemetry requires an owned fitted System");
				const auto &owner = definition.owners.at(system.key());
				const auto source = std::find_if(definition.consoles.begin(), definition.consoles.end(),
				                                 [&](const auto &console)
				                                 {
					                                 return console.console.id == owner;
				                                 });
				knowledge_require(source != definition.consoles.end() &&
				                      knowledge_has(source->producers, record.producer),
				                  name + ": telemetry producer is outside owning Console scope");
				for (const auto &existing : definition.telemetry[system.key()])
					knowledge_require(existing.schema != record.schema || existing.subject != record.subject,
					                  name + ": duplicate telemetry schema/subject for one source System");
				definition.telemetry[system.key()].push_back(std::move(record));
			}
		auto compile_sharing = [&](const Json &row, bool feed)
		{
			GameplayKnowledgeSharingDefinition sharing;
			sharing.id = row.value("id", std::string{});
			sharing.source = row.at("source_console").get<std::string>();
			sharing.target = feed ? row.at("target_console").get<std::string>() : std::string{};
			sharing.schema = compiled.knowledge.schema(row.at("schema").get<std::string>());
			sharing.subject = row.contains("subject") ? knowledge_unsigned(row.at("subject")) : 0;
			sharing.live = feed || row.value("live", false);
			const auto &schema = knowledge_schema(compiled.knowledge, sharing.schema);
			knowledge_require(feed || schema.ship_publishable, name + ": schema cannot be published shipwide");
			for (const auto &field : row.at("fields"))
			{
				const auto key = compiled.knowledge.field(sharing.schema, field.get<std::string>());
				knowledge_require(schema.fields.at(key - 1).publishable && !knowledge_has(sharing.fields, key),
				                  name + ": private or repeated sharing field");
				sharing.fields.push_back(key);
			}
			knowledge_require(!sharing.fields.empty(), name + ": sharing requires explicit fields");
			auto check_console = [&](const std::string &id)
			{
				const auto found = std::find_if(definition.consoles.begin(), definition.consoles.end(),
				                                [&](const auto &c)
				                                {
					                                return c.console.id == id;
				                                });
				knowledge_require(found != definition.consoles.end() &&
				                      knowledge_readable(compiled.knowledge, *found, sharing.schema),
				                  name + ": sharing Console cannot read schema");
			};
			check_console(sharing.source);
			if (feed)
			{
				check_console(sharing.target);
				knowledge_require(sharing.target != sharing.source, name + ": feed cannot target its source");
			}
			return sharing;
		};
		for (const auto &row : entity.value("feeds", Json::array()))
			definition.feeds.push_back(compile_sharing(row, true));
		for (const auto &row : entity.value("publications", Json::array()))
		{
			auto sharing = compile_sharing(row, false);
			knowledge_require(valid_identifier(sharing.id) &&
			                      definition.publications.emplace(sharing.id, sharing).second,
			                  name + ": invalid/duplicate publication identity");
		}
		compiled.ships.emplace(name, std::move(definition));
	}
	for (const auto &[name, row] : catalog.messages)
	{
		GameplayKnowledgeMessageDefinition message;
		message.schema = compiled.knowledge.schema(row.at("schema").get<std::string>());
		const auto &schema = knowledge_schema(compiled.knowledge, message.schema);
		const auto &destination = row.at("destination");
		message.destination_kind = knowledge_destination(destination.at("kind").get<std::string>());
		message.destination_name = destination.value("id", std::string{});
		knowledge_require(message.destination_kind == MessageDestinationKind::Ship || !message.destination_name.empty(),
		                  name + ": explicit destination required");
		if (message.destination_kind != MessageDestinationKind::Ship)
			knowledge_require(valid_identifier(message.destination_name), name + ": invalid destination identity");
		if (message.destination_kind == MessageDestinationKind::Ship)
			knowledge_require(message.destination_name.empty() || message.destination_name == "self",
			                  name + ": ship message must target self");
		message.presentation.title_key = row.at("title_key").get<std::string>();
		message.presentation.body_key = row.at("body_key").get<std::string>();
		const auto urgency = row.contains("urgency") ? knowledge_unsigned(row.at("urgency")) : 0;
		knowledge_require(urgency <= 3, name + ": urgency must be 0..3");
		message.presentation.urgency = static_cast<std::uint8_t>(urgency);
		message.presentation.automatic = row.value("automatic", true);
		message.delay =
		    row.contains("delay_ticks") ? std::max(KnowledgeTick{1}, knowledge_unsigned(row.at("delay_ticks"))) : 1;
		message.expires_after =
		    row.contains("expires_after_ticks") ? knowledge_unsigned(row.at("expires_after_ticks")) : 0;
		knowledge_require(message.expires_after == 0 || message.expires_after > message.delay,
		                  name + ": expiry precedes delivery");
		const auto request = row.value("request_key", std::string{});
		if (!request.empty())
		{
			knowledge_require(valid_identifier(request), name + ": invalid request key");
			if (!compiled.request_keys.contains(request))
				compiled.request_keys[request] = compiled.request_keys.size() + 1;
			message.request_key = compiled.request_keys.at(request);
		}
		const auto fields = row.value("fields", Json::object());
		knowledge_require(fields.is_object(), name + ": message fields must be an object");
		for (auto it = fields.begin(); it != fields.end(); ++it)
		{
			const auto key = compiled.knowledge.field(message.schema, it.key());
			if (it.value().is_object())
			{
				knowledge_dynamic_binding(it.value(), name + ".fields." + it.key(), schema.fields.at(key - 1).type);
				message.bound_fields.push_back(key);
			}
			else
				message.literal_fields.emplace(key, knowledge_scalar(it.value(), schema.fields.at(key - 1).type));
		}
		for (const auto &field : schema.fields)
			knowledge_require(!field.required || fields.contains(field.name),
			                  name + ": required message field is unbound: " + field.name);
		const auto parameters = row.value("parameters", Json::object());
		knowledge_require(parameters.is_object(), name + ": message parameters must be an object");
		for (auto it = parameters.begin(); it != parameters.end(); ++it)
		{
			if (it.value().is_object())
			{
				knowledge_dynamic_binding(it.value(), name + ".parameters." + it.key());
				message.bound_parameters.push_back(it.key());
			}
			else if (it.value().is_string())
				message.presentation.parameters.emplace(it.key(), it.value().get<std::string>());
			else if (it.value().is_boolean())
				message.presentation.parameters.emplace(it.key(), it.value().get<bool>() ? "true" : "false");
			else if (it.value().is_number_integer())
				message.presentation.parameters.emplace(it.key(), it.value().dump());
			else
				knowledge_require(false, name + ": unsupported presentation parameter binding");
		}
		compiled.messages.emplace(name, std::move(message));
	}
	for (const auto &[name, entity] : catalog.entities)
		for (const auto &template_id : entity.value("message_templates", Json::array()))
		{
			const auto id = template_id.get<std::string>();
			knowledge_require(compiled.messages.contains(id) && compiled.ships.contains(name),
			                  name + ": unknown/unfitted message template '" + id + "'");
			const auto &message = compiled.messages.at(id);
			const auto &ship = compiled.ships.at(name);
			std::vector<const GameplayKnowledgeConsoleDefinition *> recipients;
			for (const auto &console : ship.consoles)
			{
				const bool target = message.destination_kind == MessageDestinationKind::Ship ||
				                    (message.destination_kind == MessageDestinationKind::Console &&
				                     console.console.id == message.destination_name) ||
				                    (message.destination_kind == MessageDestinationKind::Station &&
				                     console.console.station == message.destination_name) ||
				                    (message.destination_kind == MessageDestinationKind::System &&
				                     ship.owners.contains(message.destination_name) &&
				                     ship.owners.at(message.destination_name) == console.console.id);
				if (target)
					recipients.push_back(&console);
			}
			knowledge_require(!recipients.empty() &&
			                      (message.destination_kind == MessageDestinationKind::Ship || recipients.size() == 1),
			                  name + ": unknown/ambiguous message destination for '" + id + "'");
			for (const auto *console : recipients)
				knowledge_require(knowledge_readable(compiled.knowledge, *console, message.schema),
				                  name + ": message schema is forbidden to its destination");
		}
	return compiled;
}
} // namespace phoenix
