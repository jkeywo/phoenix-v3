module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

module phoenix.content;

namespace phoenix
{
void verify_scenario_authoring(const Catalog &catalog, const Json &map)
{
	if (!map.contains("scenario"))
		return;
	const auto name = map.at("id").get<std::string>();
	auto require = [&](bool valid, const std::string &reason)
	{
		if (!valid)
			throw std::runtime_error("maps." + name + ".scenario: " + reason);
	};
	require(catalog.scenarios.contains(name), "missing compiled scenario definitions");
	const auto &definition = catalog.scenarios.at(name);
	const auto &source = map.at("scenario");
	require(source.is_object(), "expected object");
	const auto scenario_role = catalog.knowledge_definition.role("phoenix.scenario");
	std::map<std::string, const KnowledgeSchema *> publication_schemas;
	for (const auto &id : definition.publications)
	{
		const auto found =
		    std::find_if(catalog.knowledge_definition.schemas.begin(), catalog.knowledge_definition.schemas.end(),
		                 [&](const auto &schema)
		                 {
			                 return schema.name == id;
		                 });
		require(found != catalog.knowledge_definition.schemas.end(),
		        "publication needs declared knowledge schema " + id);
		require(std::find(found->producers.begin(), found->producers.end(), scenario_role) != found->producers.end(),
		        "publication schema forbids scenario producer " + id);
		publication_schemas.emplace(id, &*found);
	}
	std::map<EntityId, std::string> entities;
	for (const auto &e : map.at("entities"))
		entities.emplace(e.at("id").get<EntityId>(), e.at("definition").get<std::string>());
	std::map<std::string, std::vector<std::string>> slots;
	for (const auto &s : map.at("ship_slots"))
		slots.emplace(s.at("id").get<std::string>(), s.at("allowed_ships").get<std::vector<std::string>>());
	auto actor = [&](const ScenarioActor &a)
	{
		if (a.kind == ScenarioActorKind::Entity)
			require(entities.contains(a.entity), "fixed actor must reference an initial map entity");
		else if (a.kind == ScenarioActorKind::ShipSlot)
			require(slots.contains(a.name), "unknown actor ship slot " + a.name);
		else
			require(a.kind == ScenarioActorKind::Handle && valid_identifier(a.name), "invalid spawned actor handle");
	};
	for (const auto &[id, objective] : definition.objectives)
	{
		actor(objective.actor);
		actor(objective.recipients.ship);
		std::vector<std::string> hulls;
		const auto &recipient = objective.recipients.ship;
		if (recipient.kind == ScenarioActorKind::Entity)
			hulls.push_back(entities.at(recipient.entity));
		if (recipient.kind == ScenarioActorKind::ShipSlot)
			hulls = slots.at(recipient.name);
		for (const auto &hull : hulls)
		{
			const auto consoles = resolve_consoles(catalog, hull);
			for (const auto &console : objective.recipients.consoles)
				require(std::any_of(consoles.begin(), consoles.end(),
				                    [&](const auto &c)
				                    {
					                    return c.id == console;
				                    }),
				        "objective " + id + " recipient Console " + console + " missing on " + hull);
		}
	}
	std::set<std::string> configurations;
	if (source.contains("situations"))
	{
		require(source.at("situations").is_array() && source.at("situations").size() <= 4096,
		        "situations must be bounded array");
		for (const auto &s : source.at("situations"))
		{
			require(s.is_object() && s.contains("id") && s.at("id").is_string(), "situation configuration needs id");
			const auto id = s.at("id").get<std::string>();
			require(valid_identifier(id) && configurations.insert(id).second,
			        "invalid/duplicate situation configuration " + id);
			if (s.contains("start"))
				require(s.at("start").is_boolean(), "situation start must be boolean");
			if (s.contains("instance_name"))
				require(s.at("instance_name").is_string() && valid_identifier(s.at("instance_name").get<std::string>()),
				        "invalid initial situation instance_name");
		}
	}
	auto selectors = [&](const Json &bindings)
	{
		require(bindings.is_object(), "bindings must be an object");
		for (const char *field : {"objective", "region", "event"})
			if (bindings.contains(field))
				require(bindings.at(field).is_string(), std::string(field) + " selector must be text");
		const auto region = bindings.value("region", std::string{}), event = bindings.value("event", std::string{}),
		           objective = bindings.value("objective", std::string{});
		if (!region.empty())
			require(definition.region_ids.contains(region), "unknown selected region " + region);
		if (!event.empty())
			require(std::find(definition.events.begin(), definition.events.end(), event) != definition.events.end(),
			        "unknown selected event " + event);
		if (!objective.empty())
			require(valid_identifier(objective), "invalid selected objective handle");
		if (bindings.contains("actor"))
		{
			const auto &a = bindings.at("actor");
			require(a.is_object() && a.size() == 1, "selected actor needs one entity/slot/handle");
			if (a.contains("entity"))
			{
				require(a.at("entity").is_number_integer() && a.at("entity") > 0,
				        "selected actor identity must be positive");
				actor({ScenarioActorKind::Entity, a.at("entity").get<EntityId>(), {}});
			}
			else if (a.contains("slot"))
				actor({ScenarioActorKind::ShipSlot, 0, a.at("slot").get<std::string>()});
			else
				actor({ScenarioActorKind::Handle, 0, a.at("handle").get<std::string>()});
		}
	};
	std::function<void(const state_tree::Program &)> action_literals;
	action_literals = [&](const state_tree::Program &program)
	{
		for (const auto &state : program.states)
			for (const auto *tasks : {&state.entry, &state.running, &state.exit})
				for (const auto &task : *tasks)
				{
					if (task.kind != state_tree::TaskKind::Action)
						continue;
					const auto action = static_cast<GameplayTreeAction>(task.target);
					auto valid_handle = [&](std::size_t index)
					{
						if (task.arguments.at(index).source == state_tree::Source::Literal)
							require(valid_identifier(task.arguments.at(index).literal.text),
							        "action needs valid named result/instance handle");
					};
					if (action == GameplayTreeAction::SpawnEntity)
					{
						valid_handle(5);
						for (std::size_t i = 2; i <= 3; ++i)
							if (task.arguments.at(i).source == state_tree::Source::Literal)
							{
								const auto value = task.arguments.at(i).literal.number;
								require(value >= -scenario_coordinate_limit && value <= scenario_coordinate_limit,
								        "spawn coordinate/offset exceeds supported range");
							}
						if (task.arguments.at(4).source == state_tree::Source::Literal)
							require(task.arguments.at(4).literal.number >= 0 &&
							            task.arguments.at(4).literal.number < 360000,
							        "spawn heading outside 0..359999 millidegrees");
					}
					if (action == GameplayTreeAction::SpawnGroup || action == GameplayTreeAction::StartSituation)
						valid_handle(1);
					if (action == GameplayTreeAction::AddObjective)
						valid_handle(2);
					if (static_cast<GameplayTreeAction>(task.target) == GameplayTreeAction::SetObjectiveStatus &&
					    task.arguments.at(1).source == state_tree::Source::Literal)
					{
						const auto status = task.arguments.at(1).literal.number;
						require(status == static_cast<std::int64_t>(ObjectiveStatus::Active) ||
						            status == static_cast<std::int64_t>(ObjectiveStatus::Failed) ||
						            status == static_cast<std::int64_t>(ObjectiveStatus::Cancelled),
						        "set_objective_status supports activation/failure/cancellation; successful completion "
						        "requires physical execution");
					}
					if (task.arguments.empty() || task.arguments.front().source != state_tree::Source::Literal)
						continue;
					const auto &value = task.arguments.front().literal;
					if (value.type != state_tree::Type::Text)
						continue;
					const auto &id = value.text;
					switch (static_cast<GameplayTreeAction>(task.target))
					{
					case GameplayTreeAction::SpawnEntity:
						require(catalog.entities.contains(id), "unresolved spawn_entity definition " + id);
						if (task.arguments.at(1).source == state_tree::Source::Literal &&
						    !task.arguments.at(1).literal.text.empty())
							require(definition.spawn_points.contains(task.arguments.at(1).literal.text),
							        "unresolved named spawn point");
						break;
					case GameplayTreeAction::SpawnGroup:
						require(definition.spawn_groups.contains(id), "unresolved spawn_group " + id);
						break;
					case GameplayTreeAction::AddObjective:
						require(definition.objectives.contains(id), "unresolved objective " + id);
						break;
					case GameplayTreeAction::StartSituation:
						require(configurations.contains(id), "unresolved situation configuration " + id);
						break;
					case GameplayTreeAction::EmitScenarioEvent:
						require(std::find(definition.events.begin(), definition.events.end(), id) !=
						            definition.events.end(),
						        "undeclared scenario event " + id);
						break;
					case GameplayTreeAction::PublishScenarioMessage:
					{
						require(publication_schemas.contains(id), "undeclared scenario publication " + id);
						const auto &schema = *publication_schemas.at(id);
						require(schema.ship_publishable, "scenario message schema forbids ship publication " + id);
						for (const auto &[name, type] :
						     std::map<std::string, KnowledgeType>{{"amount", KnowledgeType::Integer},
						                                          {"flag", KnowledgeType::Boolean},
						                                          {"detail", KnowledgeType::Text}})
						{
							const auto field = std::find_if(schema.fields.begin(), schema.fields.end(),
							                                [&](const auto &f)
							                                {
								                                return f.name == name;
							                                });
							require(field != schema.fields.end() && field->type == type && field->publishable,
							        "scenario message schema requires publishable " + name +
							            " with supported type: " + id);
						}
						for (const auto &field : schema.fields)
							if (field.required)
								require(field.name == "amount" || field.name == "flag" || field.name == "detail",
								        "scenario message cannot populate required field " + field.name + " in " + id);
						break;
					}
					default:
						break;
					}
				}
		for (const auto &reference : program.references)
			action_literals(reference);
	};
	auto verify_tree = [&](const std::string &tree, const Json &overrides)
	{
		require(catalog.tree_programs.contains(tree) &&
		            catalog.tree_programs.at(tree).context == state_tree::Context::Scenario,
		        "selected tree is not a compiled scenario program " + tree);
		require(overrides.is_object(), "situation parameters must be object");
		auto parameters = definition.parameters;
		for (auto i = overrides.begin(); i != overrides.end(); ++i)
		{
			require(parameters.contains(i.key()), "undeclared situation parameter " + i.key());
			const auto &current = parameters.at(i.key());
			const auto &value = i.value();
			const bool correct_type = (std::holds_alternative<bool>(current) && value.is_boolean()) ||
			                          (std::holds_alternative<std::int64_t>(current) && value.is_number_integer() &&
			                           value >= -1000000000000LL && value <= 1000000000000LL) ||
			                          (std::holds_alternative<std::string>(current) && value.is_string() &&
			                           value.get_ref<const std::string &>().size() <= 4096);
			require(correct_type, "situation parameter type mismatch " + i.key());
			if (value.is_boolean())
				parameters[i.key()] = value.get<bool>();
			else if (value.is_number_integer())
				parameters[i.key()] = value.get<std::int64_t>();
			else
				parameters[i.key()] = value.get<std::string>();
		}
		std::set<std::string> declared, visited;
		auto ancestor = tree;
		while (catalog.trees.contains(ancestor) && visited.insert(ancestor).second)
		{
			const auto &t = catalog.trees.at(ancestor);
			if (t.contains("parameters"))
				for (auto i = t.at("parameters").begin(); i != t.at("parameters").end(); ++i)
					declared.insert(i.key());
			if (!t.contains("extends"))
				break;
			ancestor = t.at("extends").get<std::string>();
		}
		auto source_tree = catalog.trees.at(tree);
		for (const auto &[key, value] : parameters)
			if (declared.contains(key))
				std::visit(
				    [&](const auto &v)
				    {
					    source_tree["arguments"][key] = v;
				    },
				    value);
		action_literals(state_tree::compile(source_tree, catalog.tree_programs.at(tree).registry, catalog.trees));
	};
	if (source.contains("tree"))
	{
		require(source.at("tree").is_string(), "tree must be an authored identifier");
		verify_tree(source.at("tree").get<std::string>(), Json::object());
		selectors(source.value("bindings", Json::object()));
	}
	std::set<std::string> initial_names;
	if (source.contains("tree"))
		initial_names.insert("main");
	if (source.contains("situations"))
		for (const auto &s : source.at("situations"))
		{
			verify_tree(s.at("tree").get<std::string>(), s.value("parameters", Json::object()));
			selectors(s.value("bindings", Json::object()));
			if (s.value("start", false))
				require(initial_names.insert(s.value("instance_name", s.at("id").get<std::string>())).second,
				        "duplicate initial situation instance name");
		}
	require(initial_names.size() <= definition.maximum_instances, "initial situations exceed instance limit");
}
} // namespace phoenix
