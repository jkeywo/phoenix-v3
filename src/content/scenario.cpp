module;
#include <utility>
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
	const auto directed_catalog = compile_directed_catalog(catalog);
	return verify_scenario_authoring(catalog, map, directed_catalog);
}
void verify_scenario_authoring(const Catalog &catalog, const Json &map, const DirectedCatalog &directed_catalog,
                               SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
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
	const auto factions = compile_factions(map);
	for (const auto &[group_id, group] : definition.spawn_groups)
		for (const auto &placement : group.entities)
			factions.validate(catalog.entities.at(placement.definition).value("faction", std::string{}));
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
	for (const auto &[id, f] : definition.fact_bindings)
	{
		if (f.kind == "clock")
			continue;
		if (f.kind == "event")
		{
			if (f.subject.entity || !f.subject.name.empty())
				actor(f.subject);
			continue;
		}
		actor(f.subject);
		if (f.kind == "objective")
		{
			require(definition.objectives.contains(f.operation), "unknown objective fact " + f.operation);
			continue;
		}
		std::vector<std::string> hulls;
		if (f.subject.kind == ScenarioActorKind::Entity)
			hulls.push_back(entities.at(f.subject.entity));
		else if (f.subject.kind == ScenarioActorKind::ShipSlot)
			hulls = slots.at(f.subject.name);
		else
			for (const auto &[name, e] : catalog.entities)
				hulls.push_back(name);
		if (f.kind == "inventory")
		{
			bool found = false;
			for (const auto &h : hulls)
			{
				const auto logistics = compile_logistics(catalog, h, directed_catalog, properties);
				const bool valid = logistics.materials.contains(f.operation) &&
				                   std::any_of(logistics.stores.begin(), logistics.stores.end(),
				                               [&](const auto &s)
				                               {
					                               return s.id == f.field;
				                               });
				found |= valid;
				if (f.subject.kind != ScenarioActorKind::Handle)
					require(valid, "unknown inventory fact " + f.id);
			}
			require(found, "unknown spawned inventory fact " + f.id);
		}
		if (f.kind == "compartment")
		{
			bool found = false;
			for (const auto &h : hulls)
			{
				auto combat = compile_combat(catalog, h, directed_catalog, properties);
				const bool valid = combat && combat->compartment_ids.contains(f.field);
				found |= valid;
				if (f.subject.kind != ScenarioActorKind::Handle)
					require(valid, "unknown compartment fact " + f.id);
			}
			require(found, "unknown spawned compartment fact " + f.id);
		}
		if (f.kind == "away")
		{
			bool valid = false;
			for (const auto &m : map.value("away_missions", Json::array()))
				if (m.at("id") == f.operation && f.subject.kind == ScenarioActorKind::ShipSlot &&
				    m.at("ship_slot") == f.subject.name)
					valid = true;
			require(valid, "away fact needs matching operation and owner slot " + f.id);
		}
	}
	std::set<std::string> report_instances;
	if (source.contains("tree"))
		report_instances.insert("main");
	for (const auto &v : source.value("situations", Json::array()))
		report_instances.insert(v.value("instance_name", v.at("id").get<std::string>()));
	std::set<std::pair<std::string, std::string>> reports;
	for (const auto &r : definition.reports)
	{
		actor(r.ship);
		require(valid_identifier(r.instance) && report_instances.contains(r.instance), "unknown report instance");
		std::string key = r.ship.kind == ScenarioActorKind::Entity
		                      ? "entity:" + std::to_string(r.ship.entity)
		                      : std::to_string(static_cast<int>(r.ship.kind)) + ":" + r.ship.name;
		require(reports.emplace(r.instance, key).second, "duplicate report for ship and situation");
	}
	for (const auto &b : definition.event_bindings)
		if (b.subject)
			actor(*b.subject);
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
		for (const char *field : {"objective", "region", "event", "knowledge_console"})
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
	std::optional<GameplayKnowledgeDefinitions> reader_definitions;
	auto verify_reader = [&](const std::string &tree, const Json &bindings)
	{
		const auto reads = gameplay_record_bindings(catalog.trees.at(tree), catalog);
		const auto console_name = bindings.value("knowledge_console", std::string{});
		if (reads.empty() && console_name.empty())
			return;
		require(bindings.contains("actor") && valid_identifier(console_name),
		        "scenario record bindings require an explicit actor and knowledge_console reader");
		if (!reader_definitions)
			reader_definitions = compile_gameplay_knowledge(catalog, properties);
		std::vector<std::string> hulls;
		const auto &selected = bindings.at("actor");
		if (selected.contains("entity"))
			hulls.push_back(entities.at(selected.at("entity").get<EntityId>()));
		else if (selected.contains("slot"))
			hulls = slots.at(selected.at("slot").get<std::string>());
		else
		{
			// Spawned handles resolve at runtime. Validate a supported reader exists;
			// the bridge checks the actual hull's roles again before reading evidence.
			for (const auto &[hull, ship] : reader_definitions->ships)
				for (const auto &console : ship.consoles)
					if (console.console.id == console_name &&
					    std::all_of(reads.begin(), reads.end(),
					                [&](const auto &read)
					                {
						                const auto &schema = catalog.knowledge_definition.schemas.at(
						                    catalog.knowledge_definition.schema(read.schema) - 1);
						                return std::any_of(console.readers.begin(), console.readers.end(),
						                                   [&](auto role)
						                                   {
							                                   return std::find(schema.readers.begin(),
							                                                    schema.readers.end(),
							                                                    role) != schema.readers.end();
						                                   });
					                }))
						hulls.push_back(hull);
		}
		require(!hulls.empty(), "knowledge_console has no supported actor ship");
		for (const auto &hull : hulls)
		{
			const auto &ship = reader_definitions->ships.at(hull);
			const auto console = std::find_if(ship.consoles.begin(), ship.consoles.end(),
			                                  [&](const auto &c)
			                                  {
				                                  return c.console.id == console_name;
			                                  });
			require(console != ship.consoles.end(), "knowledge Console " + console_name + " missing on " + hull);
			for (const auto &read : reads)
			{
				const auto &schema =
				    catalog.knowledge_definition.schemas.at(catalog.knowledge_definition.schema(read.schema) - 1);
				require(std::any_of(console->readers.begin(), console->readers.end(),
				                    [&](auto role)
				                    {
					                    return std::find(schema.readers.begin(), schema.readers.end(), role) !=
					                           schema.readers.end();
				                    }),
				        "knowledge Console reader roles exclude " + read.schema + " on " + hull);
			}
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
					if (action == GameplayTreeAction::ApplyEffectGroup &&
					    task.arguments.at(2).source == state_tree::Source::Literal)
						compile_effect_group(catalog, task.arguments.at(2).literal.text, properties);
					if (action == GameplayTreeAction::ApplyShieldEffect)
					{
						valid_handle(2);
						if (task.arguments.at(1).source == state_tree::Source::Literal)
							compile_shield_effect(catalog, task.arguments.at(1).literal.text, properties);
					}
					if (action == GameplayTreeAction::RefreshShieldEffect ||
					    action == GameplayTreeAction::RemoveShieldEffect)
						valid_handle(1);
					if (action == GameplayTreeAction::ApplyModifierEffect)
					{
						valid_handle(3);
						if (task.arguments.at(2).source == state_tree::Source::Literal)
							compile_modifier_effect(catalog, task.arguments.at(2).literal.text, properties);
					}
					if (action == GameplayTreeAction::RefreshModifierEffect ||
					    action == GameplayTreeAction::RemoveModifierEffect)
						valid_handle(2);
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
					if (action == GameplayTreeAction::AddObjective ||
					    action == GameplayTreeAction::StartObjectiveSequence)
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
						factions.validate(catalog.entities.at(id).value("faction", std::string{}));
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
					case GameplayTreeAction::StartObjectiveSequence:
						require(definition.objective_sequences.contains(id), "unresolved objective sequence " + id);
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
		verify_reader(source.at("tree").get<std::string>(), source.value("bindings", Json::object()));
	}
	std::set<std::string> initial_names;
	if (source.contains("tree"))
		initial_names.insert("main");
	if (source.contains("situations"))
		for (const auto &s : source.at("situations"))
		{
			verify_tree(s.at("tree").get<std::string>(), s.value("parameters", Json::object()));
			selectors(s.value("bindings", Json::object()));
			verify_reader(s.at("tree").get<std::string>(), s.value("bindings", Json::object()));
			if (s.value("start", false))
				require(initial_names.insert(s.value("instance_name", s.at("id").get<std::string>())).second,
				        "duplicate initial situation instance name");
		}
	require(initial_names.size() <= definition.maximum_instances, "initial situations exceed instance limit");
}
namespace
{
struct LoadClosure
{
	std::set<std::string> definitions, assets;
};
LoadClosure load_closure(const Catalog &catalog, const std::string &map)
{
	LoadClosure result;
	std::function<void(const Json &)> visit;
	std::function<void(const std::string &)> reference = [&](const std::string &id)
	{
		if (catalog.assets.contains(id))
		{
			result.assets.insert(id);
			return;
		}
		for (const auto *table : {&catalog.maps, &catalog.entities, &catalog.consoles, &catalog.trees,
		                          &catalog.equipment, &catalog.assemblies, &catalog.messages, &catalog.effects,
		                          &catalog.resources, &catalog.recipes, &catalog.properties})
			if (const auto found = table->find(id); found != table->end())
			{
				if (result.definitions.insert(id).second)
					visit(found->second);
				return;
			}
	};
	visit = [&](const Json &value)
	{
		if (value.is_string())
			reference(value.get<std::string>());
		else if (value.is_object())
		{
			if (value.contains("recipe_tags") && value.at("recipe_tags").is_array())
				for (const auto &[id, recipe] : catalog.recipes)
					for (const auto &tag : value.at("recipe_tags"))
						if (recipe.contains("tags") && std::find(recipe.at("tags").begin(), recipe.at("tags").end(),
						                                         tag) != recipe.at("tags").end())
							reference(id);
			if (value.contains("capacity_tag") && value.contains("filter"))
				for (const auto &[id, resource] : compile_resource_catalog(catalog))
				{
					const auto &filter = value.at("filter");
					bool accepted = false;
					if (filter.contains("resources"))
						accepted = std::find(filter.at("resources").begin(), filter.at("resources").end(), id) !=
						           filter.at("resources").end();
					if (filter.contains("tags"))
						for (const auto &tag : filter.at("tags"))
							accepted = accepted || resource.tags.contains(tag.get<std::string>());
					if (accepted)
						reference(id);
				}
			for (const auto &[key, child] : value.items())
			{
				reference(key);
				visit(child);
			}
		}
		else if (value.is_array())
			for (const auto &child : value)
				visit(child);
	};
	if (!catalog.maps.contains(map))
		throw std::runtime_error("preload: unknown scenario " + map);
	reference(map);
	if (!catalog.loading_background.empty())
		reference(catalog.loading_background);
	const auto &source = catalog.maps.at(map);
	if (source.contains("preload"))
	{
		if (!source.at("preload").is_array())
			throw std::runtime_error("maps." + map + ".preload: expected reference array");
		for (const auto &value : source.at("preload"))
		{
			if (!value.is_string())
				throw std::runtime_error("maps." + map + ".preload: expected reference");
			const auto id = value.get<std::string>();
			if (!result.definitions.contains(id) && !result.assets.contains(id))
				throw std::runtime_error("maps." + map + ".preload: unresolved reference " + id);
		}
	}
	for (const auto &id : result.definitions)
		if (const auto program = catalog.tree_programs.find(id); program != catalog.tree_programs.end())
			for (const auto &state : program->second.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &task : *tasks)
						if (task.kind == state_tree::TaskKind::Action &&
						    static_cast<GameplayTreeAction>(task.target) == GameplayTreeAction::SpawnEntity &&
						    !task.arguments.empty() && task.arguments.front().source != state_tree::Source::Literal &&
						    (!source.contains("preload") || source.at("preload").empty()))
							throw std::runtime_error("maps." + map +
							                         ".preload: dynamic spawn needs explicit preload references");
	return result;
}
} // namespace
std::vector<std::string> scenario_definitions(const Catalog &catalog, const std::string &map)
{
	const auto closure = load_closure(catalog, map);
	return {closure.definitions.begin(), closure.definitions.end()};
}
std::vector<std::string> scenario_assets(const Catalog &catalog, const std::string &map)
{
	const auto closure = load_closure(catalog, map);
	return {closure.assets.begin(), closure.assets.end()};
}
} // namespace phoenix
