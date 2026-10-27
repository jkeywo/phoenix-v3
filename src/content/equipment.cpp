module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module phoenix.content;

namespace phoenix
{
namespace
{
constexpr std::array<const char *, 6> resource_families{"stores", "generators",      "links",
                                                        "groups", "coolant_sources", "cooling_links"};
constexpr std::array<const char *, 5> composition_fields{"systems", "resources", "assemblies", "services",
                                                         "controllers"};
[[noreturn]] void composition_error(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
std::string composition_text(const Json &record, const char *key, const std::string &path)
{
	if (!record.is_object() || !record.contains(key) || !record.at(key).is_string() ||
	    record.at(key).get<std::string>().empty())
		composition_error(path + "." + key, "expected nonempty string");
	return record.at(key).get<std::string>();
}
void composition_id(const std::string &id, const std::string &path)
{
	if (!valid_identifier(id))
		composition_error(path, "invalid or oversized identity '" + id + "'");
}
Json composition_source(const Json &entity)
{
	Json source = Json::object();
	for (const auto field : composition_fields)
		if (entity.contains(field))
			source[field] = entity.at(field);
	return source;
}
struct CompositionNames
{
	std::map<std::string, std::string> systems;
	std::map<std::string, std::map<std::string, std::string>> resources;
};
struct CompositionService
{
	std::string kind, direction, target;
};
CompositionService read_service(const Json &record, const std::string &path)
{
	CompositionService result{composition_text(record, "kind", path), composition_text(record, "direction", path), {}};
	if (result.kind != "power" && result.kind != "coolant" && result.kind != "information")
		composition_error(path + ".kind", "expected power, coolant or information");
	if (result.direction != "input" && result.direction != "output")
		composition_error(path + ".direction", "expected input or output");
	if (record.contains("target"))
		result.target = composition_text(record, "target", path);
	return result;
}
Json instantiate_equipment(const Catalog &catalog, const Json &instance, const std::string &path)
{
	if (!instance.is_object())
		composition_error(path, "expected System object");
	if (!instance.contains("template"))
		return instance;
	const auto id = composition_text(instance, "template", path);
	const auto found = catalog.equipment.find(id);
	if (found == catalog.equipment.end())
		composition_error(path + ".template", "unknown equipment '" + id + "'");
	Json result = found->second;
	if (result.contains("template"))
		composition_error("equipment." + id, "nested template reference is unsupported; use extends inheritance");
	result.erase("id");
	result.erase("extends");
	Json override = instance;
	override.erase("template");
	result.merge_patch(override);
	if (!result.contains("kind") || !result.at("kind").is_string())
		composition_error(path + ".kind", "placed equipment requires an explicit capability kind");
	result["_equipment_origin"] = {{"template", id}, {"overrides", override}};
	return result;
}
std::string renamed(const std::map<std::string, std::string> &names, const std::string &value)
{
	const auto found = names.find(value);
	return found == names.end() ? value : found->second;
}
const std::map<std::string, std::string> &family(const CompositionNames &names, const std::string &key)
{
	static const std::map<std::string, std::string> empty;
	const auto found = names.resources.find(key);
	return found == names.resources.end() ? empty : found->second;
}
std::string rewrite_reference(const Json &value, const std::map<std::string, std::string> &locals,
                              const std::map<std::string, std::string> &bindings, const std::string &path)
{
	if (!value.is_string())
		composition_error(path, "expected endpoint identity string");
	const auto text = value.get<std::string>();
	if (text.starts_with('$'))
	{
		const auto found = bindings.find(text.substr(1));
		if (found == bindings.end())
			composition_error(path, "unbound service '" + text + "'");
		return found->second;
	}
	return renamed(locals, text);
}
void rewrite_system(const Catalog &catalog, Json &value, const CompositionNames &names,
                    const std::map<std::string, std::string> &bindings, const std::string &path)
{
	if (value.is_array())
	{
		for (std::size_t i = 0; i < value.size(); ++i)
			rewrite_system(catalog, value.at(i), names, bindings, path + "[" + std::to_string(i) + "]");
		return;
	}
	if (!value.is_object())
		return;
	for (auto entry = value.begin(); entry != value.end(); ++entry)
	{
		const auto &key = entry.key();
		if (key == "processes" && entry.value().is_object())
		{
			for (auto &[slot, binding] : entry.value().items())
				if (binding.is_object() && binding.contains("native_id"))
				{
					const auto process = binding.value("process", std::string{});
					if (!catalog.effects.contains(process))
						composition_error(path + ".processes." + slot, "unknown process template");
					const auto native = catalog.effects.at(process).value("native", std::string("effects"));
					const auto &ids = native == "generation" ? family(names, "generators")
					                  : native == "cooling"  ? family(names, "cooling_links")
					                                         : names.systems;
					binding["native_id"] = rewrite_reference(binding.at("native_id"), ids, bindings,
					                                         path + ".processes." + slot + ".native_id");
				}
			continue;
		}
		const std::map<std::string, std::string> *ids = nullptr;
		if (key == "system")
			ids = &names.systems;
		else if (key == "power_store" || key == "store")
			ids = &family(names, "stores");
		else if (key == "cooling_link")
			ids = &family(names, "cooling_links");
		else if (key == "coolant_source")
			ids = &family(names, "coolant_sources");
		else if (key == "generator")
			ids = &family(names, "generators");
		else if (key == "group" || key == "power_group")
			ids = &family(names, "groups");
		else if (key == "conduit")
			ids = &family(names, "links");
		if (ids)
			entry.value() = rewrite_reference(entry.value(), *ids, bindings, path + "." + key);
		else if (key != "_equipment_origin" && key != "_assembly_origin" && key != "properties")
			rewrite_system(catalog, entry.value(), names, bindings, path + "." + key);
	}
}
void rewrite_resource(Json &record, const std::string &kind, const CompositionNames &names,
                      const std::map<std::string, std::string> &bindings, const std::string &path)
{
	auto field = [&](const char *key, const char *target)
	{
		if (record.contains(key))
			record[key] = rewrite_reference(record.at(key), family(names, target), bindings, path + "." + key);
	};
	field("id", kind.c_str());
	if (kind == "stores")
		field("group", "groups");
	else if (kind == "generators")
		field("store", "stores");
	else if (kind == "links")
	{
		field("from", "stores");
		field("to", "stores");
	}
	else if (kind == "groups")
		field("parent", "groups");
	else if (kind == "coolant_sources")
		field("power_store", "stores");
	else if (kind == "cooling_links")
	{
		field("source", "coolant_sources");
		field("store", "stores");
	}
}
CompositionNames local_names(const Json &assembly, const std::string &placement, const std::string &path)
{
	CompositionNames result;
	const auto systems = assembly.value("systems", Json::object());
	if (!systems.is_object())
		composition_error(path + ".systems", "expected object");
	for (auto it = systems.begin(); it != systems.end(); ++it)
	{
		composition_id(it.key(), path + ".systems");
		auto id = placement + "." + it.key();
		composition_id(id, path + ".systems");
		result.systems.emplace(it.key(), std::move(id));
	}
	const auto resources = assembly.value("resources", Json::object());
	if (!resources.is_object())
		composition_error(path + ".resources", "expected object");
	for (const auto kind : resource_families)
	{
		if (!resources.contains(kind))
			continue;
		if (!resources.at(kind).is_array())
			composition_error(path + ".resources." + kind, "expected array");
		for (const auto &record : resources.at(kind))
		{
			auto id = composition_text(record, "id", path + ".resources." + kind);
			composition_id(id, path + ".resources." + kind);
			auto placed = placement + "." + id;
			composition_id(placed, path + ".resources." + kind);
			if (!result.resources[kind].emplace(id, std::move(placed)).second)
				composition_error(path + ".resources." + kind, "duplicate local identity " + id);
		}
	}
	return result;
}
std::string service_local_target(const CompositionService &service, const CompositionNames &names)
{
	if (service.kind == "power")
		return renamed(family(names, "stores"), service.target);
	if (service.kind == "coolant")
		return renamed(family(names, service.direction == "output" ? "coolant_sources" : "stores"), service.target);
	return renamed(names.systems, service.target);
}
struct AssemblyPlacement
{
	std::string id, definition, path;
	Json authored, resolved;
	CompositionNames names;
};
void validate_equipment_spec(const Catalog &catalog, const Json &spec, const std::string &path,
                             SharedPropertyCatalog properties)
{
	if (!spec.is_object())
		composition_error(path, "expected System object");
	const auto kind = composition_text(spec, "kind", path);
	static const std::set<std::string> kinds{
	    "engine", "steering",     "radar",     "generator", "storage",  "distribution", "coolant", "lateral",
	    "boost",  "impulse",      "weapon",    "turret",    "magazine", "repair_team",  "team",    "shield",
	    "comms",  "displacement", "transport", "dispatch",  "ftl",      "shuttle_bay",  "psychic"};
	if (!kinds.contains(kind))
		composition_error(path + ".kind", "unsupported capability '" + kind + "'");
	if ((kind == "engine" && !spec.contains("max_speed_mm_s")) || (kind == "radar" && !spec.contains("range_mm")))
		composition_error(path, "capability is missing its required maximum/range");
	auto required_positive = [&](const char *field, ResourceAmount maximum)
	{
		if (!spec.contains(field) || !spec.at(field).is_number_integer() || spec.at(field) <= 0 ||
		    spec.at(field) > maximum)
			composition_error(path + "." + field, "expected positive integer within capability limit");
	};
	if (kind == "engine" || ((kind == "boost" || kind == "impulse") && !spec.contains("targets")))
	{
		required_positive("max_speed_mm_s", 1000000000);
		required_positive("acceleration_mm_s2", 1000000000);
	}
	if (kind == "steering")
		required_positive("turn_mdeg_s", 360000);
	if (kind == "radar")
	{
		required_positive("range_mm", 1000000000);
		if (spec.contains("long_range_mm"))
		{
			required_positive("long_range_mm", 1000000000);
			if (spec.at("long_range_mm") <= spec.at("range_mm"))
				composition_error(path + ".long_range_mm", "must exceed range_mm");
		}
	}
	if (kind == "lateral" || kind == "engine" || kind == "steering" || kind == "boost" || kind == "impulse")
	{
		auto flight = spec;
		flight["kind"] = kind;
		compile_flight(flight, path);
	}
	for (const char *key :
	     {"max_speed_mm_s", "acceleration_mm_s2", "deceleration_mm_s2", "turn_mdeg_s", "range_mm", "energy_per_second",
	      "discrete_energy", "maximum_rate", "response_rate", "charge_ticks", "charge_time_ticks"})
		if (spec.contains(key) && (!spec.at(key).is_number_integer() || spec.at(key).is_boolean() || spec.at(key) < 0 ||
		                           spec.at(key) > resource_limit))
			composition_error(path + "." + key, "expected bounded nonnegative integer");
	for (const char *key : {"enabled", "allows_override", "allow_unpowered_start"})
		if (spec.contains(key) && !spec.at(key).is_boolean())
			composition_error(path + "." + key, "expected boolean");
	for (const char *key :
	     {"power_store", "store", "generator", "group", "power_group", "cooling_link", "coolant_source", "conduit"})
		if (spec.contains(key))
		{
			const auto reference = composition_text(spec, key, path);
			composition_id(reference.starts_with('$') ? reference.substr(1) : reference, path + "." + key);
		}
	if (kind == "shield" && spec.contains("shield_effect"))
		compile_shield_effect(catalog, composition_text(spec, "shield_effect", path), properties);
	if (spec.contains("firing_audio"))
		composition_error(path + ".firing_audio", "author firing audio on its directed operating mode");
	if (spec.contains("controller"))
	{
		const auto controller = composition_text(spec, "controller", path);
		if (!catalog.trees.contains(controller))
			composition_error(path + ".controller", "unknown tree '" + controller + "'");
	}
}
void preflight_equipment(const Catalog &catalog)
{
	// Count the resolved object keys/array replacements before copying a reusable assembly
	// for every placement. This prevents a small source pack expanding to unbounded JSON.
	for (const auto &[id, entity] : catalog.entities)
	{
		const auto source =
		    entity.contains("_equipment_source") ? entity.at("_equipment_source") : composition_source(entity);
		if (!source.is_object())
			composition_error("entities." + id, "invalid composition source");
		std::size_t systems = source.value("systems", Json::object()).size(),
		            services = source.value("services", Json::object()).size();
		std::map<std::string, std::size_t> counts;
		const auto root_resources = source.value("resources", Json::object());
		for (const auto family : resource_families)
			if (root_resources.contains(family))
				counts[family] = root_resources.at(family).size();
		const auto placements = source.value("assemblies", Json::array());
		if (!placements.is_array() || placements.size() > 256)
			composition_error("entities." + id + ".assemblies", "expected at most 256 placements");
		for (const auto &p : placements)
		{
			const auto found =
			    catalog.assemblies.find(composition_text(p, "assembly", "entities." + id + ".assemblies"));
			if (found == catalog.assemblies.end())
				composition_error("entities." + id + ".assemblies", "unknown assembly");
			const auto &base = found->second;
			const auto patch = p.value("overrides", Json::object());
			if (!patch.is_object())
				composition_error("entities." + id + ".assemblies.overrides", "expected object");
			auto object_count = [&](const char *field)
			{
				std::set<std::string> keys;
				if (base.contains(field) && base.at(field).is_object())
					for (auto it = base.at(field).begin(); it != base.at(field).end(); ++it)
						keys.insert(it.key());
				if (patch.contains(field))
				{
					if (patch.at(field).is_null())
						return std::size_t{0};
					if (!patch.at(field).is_object())
						composition_error("entities." + id + ".assemblies.overrides." + field, "expected object/null");
					for (auto it = patch.at(field).begin(); it != patch.at(field).end(); ++it)
					{
						if (it.value().is_null())
							keys.erase(it.key());
						else
							keys.insert(it.key());
					}
				}
				return keys.size();
			};
			systems += object_count("systems");
			services += object_count("services");
			if (systems > 256 || services > 4096)
				composition_error("entities." + id + ".assemblies",
				                  "expanded System/service count exceeds supported bounds");
			const auto base_resources = base.value("resources", Json::object());
			const auto replacement = patch.value("resources", Json::object());
			if (!replacement.is_object() && !replacement.is_null())
				composition_error("entities." + id + ".assemblies.overrides.resources", "expected object/null");
			for (const auto family : resource_families)
			{
				if (replacement.is_null())
					continue;
				const Json *array = nullptr;
				if (replacement.is_object() && replacement.contains(family))
					array = &replacement.at(family);
				else if (base_resources.contains(family))
					array = &base_resources.at(family);
				if (array && !array->is_null())
				{
					if (!array->is_array())
						composition_error("entities." + id + ".assemblies.resources." + family, "expected array");
					counts[family] += array->size();
				}
				if (counts[family] > 4096)
					composition_error("entities." + id + ".assemblies.resources." + family,
					                  "expanded resource count exceeds 4096");
			}
		}
		if (systems > 256 || services > 4096)
			composition_error("entities." + id, "System/service count exceeds supported bounds");
		for (const auto &[family, count] : counts)
			if (count > 4096)
				composition_error("entities." + id + ".resources." + family, "resource count exceeds 4096");
	}
}
void validate_service_targets(const Json &entity, const std::map<std::string, CompositionService> &services,
                              const std::string &path)
{
	std::map<std::string, std::set<std::string>> ids;
	if (entity.contains("systems"))
		for (auto it = entity.at("systems").begin(); it != entity.at("systems").end(); ++it)
			ids["systems"].insert(it.key());
	if (entity.contains("resources"))
		for (const auto kind : resource_families)
			if (entity.at("resources").contains(kind))
				for (const auto &r : entity.at("resources").at(kind))
					ids[kind].insert(composition_text(r, "id", path + ".resources." + kind));
	for (const auto &[id, service] : services)
	{
		if (service.kind == "information")
		{
			composition_id(service.target, path + ".services." + id + ".target");
			// Console/System blackboard endpoints have their own scope-aware validator.
			continue;
		}
		const auto family_name = service.kind == "power"         ? "stores"
		                         : service.kind == "information" ? "systems"
		                         : service.direction == "output" ? "coolant_sources"
		                                                         : "stores";
		if (service.target.empty() || !ids[family_name].contains(service.target))
			composition_error(path + ".services." + id + ".target",
			                  "unresolved or incompatible " + service.kind + " endpoint '" + service.target + "'");
	}
}
} // namespace

namespace
{
void validate_assembly_definition(const Catalog &catalog, const std::string &id, const Json &assembly,
                                  SharedPropertyCatalog properties)
{
	const auto path = "assemblies." + id;
	if (assembly.contains("assemblies") &&
	    (!assembly.at("assemblies").is_array() || !assembly.at("assemblies").empty()))
		composition_error(path, "nested assemblies are unsupported");
	const auto systems = assembly.value("systems", Json::object()),
	           declared = assembly.value("services", Json::object());
	if (!systems.is_object() || systems.size() > 256)
		composition_error(path + ".systems", "expected at most 256 local Systems");
	if (!declared.is_object() || declared.size() > 4096)
		composition_error(path + ".services", "expected at most 4096 service declarations");
	Json resources = assembly.value("resources", Json::object());
	if (!resources.is_object())
		composition_error(path + ".resources", "expected object");
	CompositionNames names;
	for (auto it = systems.begin(); it != systems.end(); ++it)
	{
		composition_id(it.key(), path + ".systems");
		names.systems[it.key()] = it.key();
	}
	for (const auto kind : resource_families)
		if (resources.contains(kind))
		{
			if (!resources.at(kind).is_array() || resources.at(kind).size() > 4096)
				composition_error(path + ".resources." + kind, "expected bounded resource array");
			for (const auto &record : resources.at(kind))
			{
				const auto name = composition_text(record, "id", path + ".resources." + kind);
				names.resources[kind][name] = name;
			}
		}
	std::map<std::string, std::string> bound;
	std::map<std::string, CompositionService> local_services;
	// Unbound assembly services exist only for this structural validation. Their
	// reserves use a private catalogue; fitted ships still require authored IDs.
	auto network_resources = compile_resource_catalog(catalog);
	std::map<std::string, std::string> placeholder_resources;
	for (const auto *tag : {"energy", "coolant"})
	{
		std::string resource = std::string("__assembly_validation.") + tag;
		while (network_resources.contains(resource))
			resource += "_";
		network_resources.emplace(resource, ResourceDefinition{resource, resource, {tag}, {{tag, 1}}, {}});
		placeholder_resources.emplace(tag, resource);
	}
	std::size_t external = 0;
	for (auto it = declared.begin(); it != declared.end(); ++it)
	{
		composition_id(it.key(), path + ".services");
		auto service = read_service(it.value(), path + ".services." + it.key());
		if (!service.target.empty())
		{
			bound[it.key()] = service.target;
			local_services[it.key()] = service;
			continue;
		}
		std::string endpoint;
		do
		{
			endpoint = "__external." + std::to_string(external++);
		} while (names.systems.contains(endpoint) || family(names, "stores").contains(endpoint) ||
		         family(names, "coolant_sources").contains(endpoint));
		bound[it.key()] = endpoint;
		if (service.kind == "information")
			continue;
		const auto target = service.kind == "coolant" && service.direction == "input" ? "coolant_sources" : "stores";
		if (!resources.contains(target))
			resources[target] = Json::array();
		if (std::string_view(target) == "stores")
			resources[target].push_back({{"id", endpoint},
			                             {"resource", placeholder_resources.at("energy")},
			                             {"capacity", resource_limit},
			                             {"charge_rate", resource_limit},
			                             {"discharge_rate", resource_limit}});
		else
			resources[target].push_back({{"id", endpoint},
			                             {"resource", placeholder_resources.at("coolant")},
			                             {"capacity", resource_limit},
			                             {"initial", resource_limit},
			                             {"removal_rate", resource_limit}});
	}
	std::function<void(const Json &, const std::string &)> bindings = [&](const Json &spec, const std::string &at)
	{
		if (spec.is_array())
		{
			for (const auto &v : spec)
				bindings(v, at);
			return;
		}
		if (!spec.is_object())
			return;
		for (auto it = spec.begin(); it != spec.end(); ++it)
		{
			const std::map<std::string, std::string> *ids = nullptr;
			const auto &key = it.key();
			if (key == "system")
				ids = &names.systems;
			else if (key == "power_store" || key == "store")
				ids = &family(names, "stores");
			else if (key == "generator")
				ids = &family(names, "generators");
			else if (key == "group" || key == "power_group")
				ids = &family(names, "groups");
			else if (key == "cooling_link")
				ids = &family(names, "cooling_links");
			else if (key == "coolant_source")
				ids = &family(names, "coolant_sources");
			else if (key == "conduit")
				ids = &family(names, "links");
			if (ids)
			{
				if (!it.value().is_string())
					composition_error(at + "." + key, "expected local identity or declared service placeholder");
				const auto value = it.value().get<std::string>();
				if (value.starts_with('$'))
				{
					if (!bound.contains(value.substr(1)))
						composition_error(at + "." + key, "undeclared external service '" + value + "'");
				}
				else if (!ids->contains(value))
					composition_error(at + "." + key, "unresolved local reference '" + value +
					                                      "'; declare an external service when required");
			}
			else if (key != "_equipment_origin" && key != "_assembly_origin" && key != "properties")
				bindings(it.value(), at + "." + key);
		}
	};
	Json fitted = Json::object();
	for (auto it = systems.begin(); it != systems.end(); ++it)
	{
		auto spec = instantiate_equipment(catalog, it.value(), path + ".systems." + it.key());
		validate_equipment_spec(catalog, spec, path + ".systems." + it.key(), properties);
		bindings(spec, path + ".systems." + it.key());
		fitted[it.key()] = std::move(spec);
	}
	for (const auto kind : resource_families)
		if (resources.contains(kind))
			for (auto &record : resources[kind])
				rewrite_resource(record, kind, names, bound, path + ".resources." + kind);
	// Propagate a local circuit's exact resource into each connected external
	// placeholder. Conflicting authored resources are rejected by the compiler.
	if (resources.contains("stores") && resources.contains("links"))
	{
		std::map<std::string, Json *> stores;
		for (auto &store : resources["stores"])
			stores.emplace(store.at("id").get<std::string>(), &store);
		for (std::size_t pass = 0; pass < stores.size(); ++pass)
		{
			bool changed = false;
			for (const auto &link : resources["links"])
			{
				const auto from = stores.find(link.at("from").get<std::string>());
				const auto to = stores.find(link.at("to").get<std::string>());
				if (from == stores.end() || to == stores.end())
					continue;
				for (const auto [target, other] :
				     {std::pair{from->second, to->second}, std::pair{to->second, from->second}})
					if (target->at("resource") == placeholder_resources.at("energy") &&
					    other->at("resource") != placeholder_resources.at("energy"))
					{
						(*target)["resource"] = other->at("resource");
						changed = true;
					}
			}
			if (!changed)
				break;
		}
	}
	if (!resources.empty())
		compile_resource_network(resources, network_resources);
	validate_service_targets(Json{{"systems", fitted}, {"resources", resources}}, local_services, path);
	if (assembly.contains("controllers"))
	{
		if (!assembly.at("controllers").is_object())
			composition_error(path + ".controllers", "expected object");
		for (auto it = assembly.at("controllers").begin(); it != assembly.at("controllers").end(); ++it)
		{
			if (!systems.contains(it.key()) || !it.value().is_string() ||
			    !catalog.trees.contains(it.value().get<std::string>()))
				composition_error(path + ".controllers." + it.key(), "unresolved local System or controller tree");
		}
	}
}
} // namespace

Json authored_equipment_source(Json resolved)
{
	if (!resolved.contains("_equipment_source"))
		return resolved;
	const auto source = resolved.at("_equipment_source");
	if (!source.is_object())
		composition_error("equipment.source", "invalid retained composition source");
	for (const auto field : composition_fields)
	{
		resolved.erase(field);
		if (source.contains(field))
			resolved[field] = source.at(field);
	}
	resolved.erase("_equipment_source");
	return resolved;
}

void resolve_equipment(Catalog &catalog, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	for (const auto &[id, equipment] : catalog.equipment)
	{
		composition_id(id, "equipment." + id);
		if (!equipment.is_object())
			composition_error("equipment." + id, "expected object");
		if (equipment.contains("extends"))
			composition_error("equipment." + id, "inheritance must resolve before equipment placement");
		if (equipment.contains("template"))
			composition_error("equipment." + id, "use extends for template inheritance");
		validate_equipment_spec(catalog, equipment, "equipment." + id, properties);
	}
	for (const auto &[id, assembly] : catalog.assemblies)
	{
		composition_id(id, "assemblies." + id);
		if (!assembly.is_object())
			composition_error("assemblies." + id, "expected object");
		if (assembly.contains("extends"))
			composition_error("assemblies." + id, "inheritance must resolve before assembly placement");
		if (assembly.contains("assemblies") && !assembly.at("assemblies").empty())
			composition_error("assemblies." + id, "nested assemblies are unsupported; flatten the assembly explicitly");
		validate_assembly_definition(catalog, id, assembly, properties);
	}
	preflight_equipment(catalog);
	auto expanded = catalog.entities;
	for (auto &[entity_id, entity] : expanded)
	{
		const auto path = "entities." + entity_id;
		bool uses_composition =
		    entity.contains("assemblies") || entity.contains("services") || entity.contains("_equipment_source");
		if (entity.contains("systems") && entity.at("systems").is_object())
			for (const auto &spec : entity.at("systems"))
				uses_composition |= spec.is_object() && spec.contains("template");
		if (!uses_composition)
			continue;
		const auto original =
		    entity.contains("_equipment_source") ? entity.at("_equipment_source") : composition_source(entity);
		if (!original.is_object() || original.contains("_equipment_source"))
			composition_error(path, "invalid composition source metadata");
		for (const auto field : composition_fields)
		{
			entity.erase(field);
			if (original.contains(field))
				entity[field] = original.at(field);
		}
		if (!entity.contains("systems"))
			entity["systems"] = Json::object();
		if (!entity.at("systems").is_object())
			composition_error(path + ".systems", "expected object");
		if (entity.contains("controllers") && !entity.at("controllers").is_object())
			composition_error(path + ".controllers", "expected object");
		const auto explicit_controllers = entity.value("controllers", Json::object());
		for (auto it = entity["systems"].begin(); it != entity["systems"].end(); ++it)
		{
			composition_id(it.key(), path + ".systems");
			it.value() = instantiate_equipment(catalog, it.value(), path + ".systems." + it.key());
			if (it.value().contains("controller") &&
			    (!entity.contains("controllers") || !entity.at("controllers").contains(it.key())))
				entity["controllers"][it.key()] = it.value().at("controller");
		}
		std::map<std::string, CompositionService> services;
		if (entity.contains("services"))
		{
			if (!entity.at("services").is_object())
				composition_error(path + ".services", "expected object");
			for (auto it = entity.at("services").begin(); it != entity.at("services").end(); ++it)
			{
				composition_id(it.key(), path + ".services");
				services.emplace(it.key(), read_service(it.value(), path + ".services." + it.key()));
			}
		}
		std::vector<AssemblyPlacement> placements;
		std::set<std::string> placement_ids;
		if (entity.contains("assemblies"))
		{
			if (!entity.at("assemblies").is_array() || entity.at("assemblies").size() > 256)
				composition_error(path + ".assemblies", "expected array of at most 256 placements");
			for (const auto &placed : entity.at("assemblies"))
			{
				AssemblyPlacement p;
				p.path = path + ".assemblies";
				p.id = composition_text(placed, "id", p.path);
				composition_id(p.id, p.path);
				p.path += "." + p.id;
				if (!placement_ids.insert(p.id).second)
					composition_error(p.path, "duplicate placement identity");
				p.definition = composition_text(placed, "assembly", p.path);
				const auto found = catalog.assemblies.find(p.definition);
				if (found == catalog.assemblies.end())
					composition_error(p.path, "unknown assembly '" + p.definition + "'");
				p.authored = placed;
				p.resolved = found->second;
				if (placed.contains("overrides"))
				{
					if (!placed.at("overrides").is_object())
						composition_error(p.path + ".overrides", "expected merge-patch object");
					p.resolved.merge_patch(placed.at("overrides"));
				}
				if (p.resolved.contains("assemblies") && !p.resolved.at("assemblies").empty())
					composition_error(p.path, "nested assemblies are unsupported");
				p.names = local_names(p.resolved, p.id, p.path);
				const auto local_services = p.resolved.value("services", Json::object());
				if (!local_services.is_object())
					composition_error(p.path + ".services", "expected object");
				for (auto it = local_services.begin(); it != local_services.end(); ++it)
				{
					composition_id(it.key(), p.path + ".services");
					auto service = read_service(it.value(), p.path + ".services." + it.key());
					if (service.target.empty())
						continue;
					service.target = service_local_target(service, p.names);
					const auto name = p.id + "." + it.key();
					composition_id(name, p.path + ".services");
					if (!services.emplace(name, service).second)
						composition_error(p.path + ".services", "duplicate exposed service '" + name + "'");
					entity["services"][name] = {
					    {"kind", service.kind},
					    {"direction", service.direction},
					    {"target", service.target},
					    {"_assembly_origin",
					     {{"assembly", p.definition}, {"placement", p.id}, {"local_id", it.key()}}}};
				}
				placements.push_back(std::move(p));
			}
		}
		for (const auto &p : placements)
		{
			std::map<std::string, std::string> bound;
			const auto declared = p.resolved.value("services", Json::object()),
			           bindings = p.authored.value("bindings", Json::object());
			if (!bindings.is_object())
				composition_error(p.path + ".bindings", "expected object");
			for (auto it = bindings.begin(); it != bindings.end(); ++it)
			{
				if (!declared.contains(it.key()) || !it.value().is_string())
					composition_error(p.path + ".bindings." + it.key(),
					                  "unknown service or non-string external endpoint");
				const auto local = read_service(declared.at(it.key()), p.path + ".services." + it.key());
				const auto external = services.find(it.value().get<std::string>());
				if (external == services.end())
					composition_error(p.path + ".bindings." + it.key(), "unresolved external service");
				if (local.kind != external->second.kind || local.direction == external->second.direction)
					composition_error(p.path + ".bindings." + it.key(),
					                  "binding requires the same kind and opposite directions");
				bound[it.key()] = external->second.target;
			}
			for (auto it = declared.begin(); it != declared.end(); ++it)
			{
				if (bound.contains(it.key()))
					continue;
				const auto service = read_service(it.value(), p.path + ".services." + it.key());
				if (service.target.empty())
					composition_error(p.path + ".bindings." + it.key(), "required imported service has no binding");
				bound[it.key()] = service_local_target(service, p.names);
			}
			const auto local_systems = p.resolved.value("systems", Json::object());
			for (auto it = local_systems.begin(); it != local_systems.end(); ++it)
			{
				const auto placed_id = p.names.systems.at(it.key());
				if (entity["systems"].contains(placed_id))
					composition_error(p.path, "placed System collides with '" + placed_id + "'");
				auto spec = instantiate_equipment(catalog, it.value(), p.path + ".systems." + it.key());
				rewrite_system(catalog, spec, p.names, bound, p.path + ".systems." + it.key());
				spec["_assembly_origin"] = {{"assembly", p.definition},
				                            {"placement", p.id},
				                            {"local_id", it.key()},
				                            {"overrides", p.authored.value("overrides", Json::object())}};
				if (spec.contains("controller") &&
				    (!entity.contains("controllers") || !entity.at("controllers").contains(placed_id)))
					entity["controllers"][placed_id] = spec.at("controller");
				entity["systems"][placed_id] = std::move(spec);
			}
			const auto local_resources = p.resolved.value("resources", Json::object());
			for (const auto kind : resource_families)
				if (local_resources.contains(kind))
				{
					if (!entity.contains("resources"))
						entity["resources"] = Json::object();
					if (!entity["resources"].is_object())
						composition_error(path + ".resources", "expected object");
					if (!entity["resources"].contains(kind))
						entity["resources"][kind] = Json::array();
					if (!entity["resources"][kind].is_array())
						composition_error(path + ".resources." + kind, "expected array");
					for (auto record : local_resources.at(kind))
					{
						const auto local_id = composition_text(record, "id", p.path + ".resources." + kind);
						rewrite_resource(record, kind, p.names, bound, p.path + ".resources." + kind + "." + local_id);
						record["_assembly_origin"] = {
						    {"assembly", p.definition}, {"placement", p.id}, {"local_id", local_id}};
						entity["resources"][kind].push_back(std::move(record));
					}
				}
			if (p.resolved.contains("controllers"))
			{
				if (!p.resolved.at("controllers").is_object())
					composition_error(p.path + ".controllers", "expected object");
				for (auto it = p.resolved.at("controllers").begin(); it != p.resolved.at("controllers").end(); ++it)
				{
					if (!p.names.systems.contains(it.key()))
						composition_error(p.path + ".controllers", "unknown local System '" + it.key() + "'");
					const auto placed_id = p.names.systems.at(it.key());
					if (!explicit_controllers.contains(placed_id))
						entity["controllers"][placed_id] = it.value();
				}
			}
		}
		if (entity.contains("resources"))
		{
			if (!entity.at("resources").is_object())
				composition_error(path + ".resources", "expected object");
			for (const auto kind : resource_families)
				if (entity.at("resources").contains(kind))
				{
					if (!entity.at("resources").at(kind).is_array())
						composition_error(path + ".resources." + kind, "expected array");
					std::set<std::string> seen;
					for (const auto &record : entity.at("resources").at(kind))
					{
						const auto id = composition_text(record, "id", path + ".resources." + kind);
						if (!seen.insert(id).second)
							composition_error(path + ".resources." + kind, "duplicate placed identity '" + id + "'");
					}
				}
		}
		validate_service_targets(entity, services, path);
		entity["_equipment_source"] = original;
	}
	catalog.entities = std::move(expanded);
}
} // namespace phoenix
