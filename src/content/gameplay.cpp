module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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
[[noreturn]] void fitting_error(const std::string &path, const std::string &reason)
{
	throw std::runtime_error(path + ": " + reason);
}
std::string fitting_text(const Json &j, const char *field, const std::string &path)
{
	if (!j.is_object() || !j.contains(field) || !j.at(field).is_string() || j.at(field).get<std::string>().empty())
		fitting_error(path + "." + field, "expected nonempty string");
	return j.at(field).get<std::string>();
}
ResourceAmount fitting_amount(const Json &j, const char *field, const std::string &path,
                              ResourceAmount maximum = resource_limit)
{
	if (!j.contains(field))
		return 0;
	const auto &v = j.at(field);
	if (!v.is_number_integer() || v.is_boolean() || v < 0 || v > maximum)
		fitting_error(path + "." + field, "expected bounded nonnegative integer");
	return v.get<ResourceAmount>();
}
ResourceIndex fitting_reference(const Json &j, const char *field, const std::map<std::string, ResourceIndex> &ids,
                                const std::string &path)
{
	if (!j.contains(field))
		return no_resource;
	const auto id = fitting_text(j, field, path);
	auto at = ids.find(id);
	if (at == ids.end())
		fitting_error(path + "." + field, "unresolved endpoint '" + id + "'");
	return at->second;
}
void verify_gameplay_panel(const Json &owner, const std::string &path)
{
	if (!owner.contains("gameplay_panel"))
		return;
	const auto &panel = owner.at("gameplay_panel");
	if (!panel.is_object())
		fitting_error(path + ".gameplay_panel", "expected normalized rectangle");
	auto value = [&](const char *key)
	{
		if (!panel.contains(key) || !panel.at(key).is_number())
			fitting_error(path + ".gameplay_panel." + key, "expected finite number");
		const auto n = panel.at(key).get<double>();
		if (!std::isfinite(n) || n < 0 || n > 1)
			fitting_error(path + ".gameplay_panel." + key, "outside normalized 0..1 bounds");
		return n;
	};
	const auto x = value("x"), y = value("y"), width = value("width"), height = value("height");
	if (width <= 0 || height <= 0 || x + width > 1 || y + height > 1)
		fitting_error(path + ".gameplay_panel", "positive rectangle must fit inside the Console");
}
} // namespace

bool uses_advanced_machinery(const Catalog &catalog, const std::string &entity_id)
{
	const auto &entity = catalog.entities.at(entity_id);
	const auto fitted = resolve_systems(catalog, entity_id);
	if (entity.contains("motion_model"))
	{
		const auto model = entity.at("motion_model").get<std::string>();
		if (model != "vector" && model != "inertial")
			fitting_error("entities." + entity_id + ".motion_model", "expected vector or inertial");
		if (model == "inertial")
			return true;
		if (entity.contains("resources") || (entity.contains("processors") && !entity.at("processors").empty()))
			fitting_error("entities." + entity_id + ".motion_model",
			              "vector mechanisms cannot bind a resource network or processors");
		for (const auto &system : fitted)
		{
			const auto &spec = entity.at("systems").at(system.id);
			if (system.kind != OperatingSystem::Engine && system.kind != OperatingSystem::Steering &&
			    system.kind != OperatingSystem::Radar)
				fitting_error("entities." + entity_id + ".motion_model",
				              "vector model supports engine, steering and radar only");
			for (const auto *field : {"reverse_speed_mm_s", "boost", "impulse", "power_rate", "heat_rate",
			                          "energy_per_second", "discrete_energy", "lateral_acceleration_mm_s2",
			                          "power_store", "generator", "power_group", "cooling_link", "conduit"})
				if (spec.contains(field))
					fitting_error("entities." + entity_id + ".systems." + system.id + "." + field,
					              "requires inertial/resource machinery");
		}
		return false;
	}
	bool advanced = entity.contains("resources") || entity.contains("presets") || entity.contains("control_layout") ||
	                (entity.contains("processors") && !entity.at("processors").empty());
	for (const auto &system : fitted)
	{
		const auto &spec = entity.at("systems").at(system.id);
		advanced |= static_cast<unsigned>(system.kind) > static_cast<unsigned>(OperatingSystem::Radar) ||
		            catalog.tree_programs.contains(system.controller);
		for (const char *field : {"reverse_speed_mm_s", "boost", "impulse", "records", "power_rate", "heat_rate",
		                          "energy_per_second", "discrete_energy"})
			advanced |= spec.contains(field);
	}
	return advanced;
}

void verify_gameplay_fitting(const Catalog &catalog, const std::string &entity_id)
{
	const DirectedCatalog directed_catalog{{entity_id, compile_directed(catalog, entity_id)}};
	return verify_gameplay_fitting(catalog, entity_id, directed_catalog);
}
void verify_gameplay_fitting(const Catalog &catalog, const std::string &entity_id,
                             const DirectedCatalog &directed_catalog)
{
	const auto &entity = catalog.entities.at(entity_id);
	const auto path = "entities." + entity_id;
	const auto fitted = resolve_systems(catalog, entity_id);
	const auto &directed = directed_catalog.at(entity_id);
	const auto consoles = resolve_consoles(catalog, entity_id);
	std::map<std::string, const FittedSystemSpec *> systems;
	std::map<std::string, const ConsoleSpec *> console_ids;
	std::map<std::string, std::string> owners;
	for (const auto &system : fitted)
		systems.emplace(system.id, &system);
	for (const auto &console : consoles)
	{
		console_ids.emplace(console.id, &console);
		for (const auto &system : console.controlled_systems)
			owners.emplace(system, console.id);
		if (catalog.consoles.contains(console.definition))
			verify_gameplay_panel(catalog.consoles.at(console.definition), "consoles." + console.definition);
	}
	if (entity.contains("stations"))
		for (const auto &station : entity.at("stations"))
			for (const auto &console : station.at("consoles"))
				verify_gameplay_panel(console, path + ".stations." + station.at("id").get<std::string>() +
				                                   ".consoles." + console.at("id").get<std::string>());
	if (catalog.presentation)
		return;
	const bool advanced = uses_advanced_machinery(catalog, entity_id);
	std::optional<ResourceNetworkDefinition> compiled;
	if (entity.contains("resources") || (entity.contains("processors") && !entity.at("processors").empty()))
	{
		const auto cached = catalog.resource_definitions.find(entity_id);
		compiled =
		    cached == catalog.resource_definitions.end()
		        ? compile_resource_network(entity.value("resources", Json::object()), compile_resource_catalog(catalog))
		        : cached->second;
	}
	const ResourceNetworkDefinition empty;
	const auto &resources = compiled ? *compiled : empty;
	std::map<std::pair<std::string, ResourceIndex>, std::string> writers;
	std::map<ResourceIndex, std::vector<std::string>> continuous_consumers;
	auto writer = [&](const char *family, ResourceIndex index, const std::string &system)
	{
		if (index == no_resource)
			return;
		const auto key = std::make_pair(std::string(family), index);
		if (!writers.emplace(key, system).second)
			fitting_error(path + ".systems." + system,
			              "endpoint already has controlling System '" + writers.at(key) + "'");
	};
	auto starting_supply = [&](ResourceIndex target)
	{
		const auto usable = [&](ResourceIndex i)
		{
			const auto &s = resources.stores[i];
			return s.enabled && (!s.stop_heat || s.initial_heat < s.stop_heat);
		};
		if (!usable(target) || !resources.stores[target].discharge_rate)
			return false;
		std::set<ResourceIndex> visited;
		std::vector<ResourceIndex> pending{target};
		while (!pending.empty())
		{
			const auto at = pending.back();
			pending.pop_back();
			if (!visited.insert(at).second)
				continue;
			const auto &store = resources.stores[at];
			if (!usable(at))
				continue;
			if (store.initial > 0 && store.discharge_rate > 0)
				return true;
			for (const auto &generator : resources.generators)
				if (generator.store == at && generator.enabled && generator.initial_output > 0 &&
				    store.charge_rate > 0 && store.discharge_rate > 0)
					return true;
			if (at == target && !store.charge_rate)
				continue;
			if (at == target)
			{
				bool reserved = true;
				for (auto group = store.group; group != no_resource; group = resources.groups[group].parent)
					reserved &= resources.groups[group].budget_rate > 0;
				if (!reserved)
					continue;
			}
			for (const auto &link : resources.links)
				if (link.to == at && !link.isolated && link.capacity_rate > 0)
					pending.push_back(link.from);
		}
		return false;
	};
	for (const auto &system : fitted)
	{
		const auto &spec = entity.at("systems").at(system.id);
		const auto at = path + ".systems." + system.id;
		const auto kind = spec.value("kind", system.id);
		const bool flight =
		    kind == "engine" || kind == "steering" || kind == "lateral" || kind == "boost" || kind == "impulse";
		ResourceAmount operating = fitting_amount(spec, "energy_per_second", at),
		               discrete = fitting_amount(spec, "discrete_energy", at), thermal = 0;
		if (directed.contains(system.id))
			for (const auto &mode : directed.at(system.id).modes)
			{
				operating =
				    std::max(operating, mode.energy_per_second + (system.kind == OperatingSystem::Radar
				                                                      ? fitting_amount(spec, "energy_per_second", at)
				                                                      : 0));
				discrete = std::max(discrete, mode.discrete_energy);
			}
		if (flight && advanced)
		{
			auto definition = spec;
			definition["kind"] = kind;
			const auto compiled_flight = compile_flight(definition, at);
			operating = std::max({operating, compiled_flight.power_rate, compiled_flight.boost_power_rate,
			                      compiled_flight.impulse_charge_power_rate, compiled_flight.impulse_power_rate});
			thermal = std::max(
			    {compiled_flight.heat_rate, compiled_flight.boost_heat_rate, compiled_flight.impulse_heat_rate});
		}
		const auto store = fitting_reference(spec, "power_store", resources.store_ids, at);
		const auto generator = fitting_reference(spec, "generator", resources.generator_ids, at);
		auto group = fitting_reference(spec, "power_group", resources.group_ids, at);
		const auto alternate_group = fitting_reference(spec, "group", resources.group_ids, at);
		if (group != no_resource && alternate_group != no_resource && group != alternate_group)
			fitting_error(at, "group and power_group disagree");
		if (group == no_resource)
			group = alternate_group;
		const auto conduit = fitting_reference(spec, "conduit", resources.link_ids, at);
		const auto cooling = fitting_reference(spec, "cooling_link", resources.cooling_link_ids, at);
		fitting_reference(spec, "coolant_source", resources.coolant_source_ids, at);
		if ((operating || discrete || thermal) && store == no_resource)
			fitting_error(at + ".power_store",
			              "power-consuming or heat-producing equipment requires an explicit local reserve");
		if (operating && store != no_resource)
			continuous_consumers[store].push_back(system.id);
		if (kind == "generator" && generator == no_resource)
			fitting_error(at + ".generator", "generator controls need a fitted provider");
		if (kind == "storage" && store == no_resource)
			fitting_error(at + ".power_store", "storage controls need a fitted store");
		if (kind == "distribution" && group == no_resource && conduit == no_resource)
			fitting_error(at, "distribution controls need a group or conduit");
		if (kind == "coolant" && cooling == no_resource)
			fitting_error(at + ".cooling_link", "coolant controls need a fitted receiving link");
		if (generator != no_resource)
		{
			if (kind != "generator")
				fitting_error(at + ".generator", "provider output belongs to a generator System");
			if (store != no_resource && store != resources.generators[generator].store)
				fitting_error(at + ".power_store", "generator capacitor binding disagrees with provider");
			writer("generator", generator, system.id);
		}
		if (group != no_resource)
		{
			if (kind != "distribution")
				fitting_error(at + ".power_group", "allocation belongs to a distribution System");
			writer("group", group, system.id);
		}
		if (conduit != no_resource)
		{
			if (kind != "distribution")
				fitting_error(at + ".conduit", "isolation belongs to a distribution System");
			writer("conduit", conduit, system.id);
		}
		if (cooling != no_resource)
		{
			if (kind != "coolant")
				fitting_error(at + ".cooling_link", "cooling allocation belongs to a coolant System");
			writer("cooling", cooling, system.id);
		}
		if (kind == "storage")
			writer("storage-control", store, system.id);
		if (spec.contains("powerless_start") && !spec.at("powerless_start").is_boolean())
			fitting_error(at + ".powerless_start", "expected explicit Boolean exception");
		if ((operating || discrete) && !catalog.presentation)
		{
			const auto &local = resources.stores[store];
			if (discrete > local.capacity ||
			    discrete > (local.discharge_rate + resource_tick_hz - 1) / resource_tick_hz)
				fitting_error(at + ".discrete_energy",
				              "atomic action cannot fit its store or per-tick discharge capability");
			if (!spec.value("powerless_start", false) && !starting_supply(store))
				fitting_error(at + ".power_store", "declared start cannot pay baseline operation; connect an enabled "
				                                   "initial supply/reserve or explicitly declare powerless_start");
		}
		if (!system.controller.empty())
		{
			if (!catalog.trees.contains(system.controller))
				fitting_error(at + ".controller", "unresolved ordinary controller");
			const auto program = catalog.tree_programs.find(system.controller);
			if (program != catalog.tree_programs.end() && program->second.context == state_tree::Context::Scenario)
				fitting_error(at + ".controller", "scenario trees cannot operate machinery");
		}
	}
	for (ResourceIndex source = 0; source < resources.coolant_sources.size(); ++source)
	{
		const auto &provider = resources.coolant_sources[source];
		if (provider.power_store == no_resource)
			continue;
		const auto &consumers = continuous_consumers[provider.power_store];
		if (consumers.size() != 1 || systems.at(consumers.front())->kind != OperatingSystem::Coolant)
			fitting_error(
			    path + ".resources.coolant_sources." + provider.id + ".power_store",
			    "powered coolant needs a dedicated local reserve with exactly one positive-rate coolant pump consumer");
	}
	if (entity.contains("services"))
	{
		if (!entity.at("services").is_object())
			fitting_error(path + ".services", "expected endpoint object");
		for (auto it = entity.at("services").begin(); it != entity.at("services").end(); ++it)
		{
			const auto kind = fitting_text(it.value(), "kind", path + ".services." + it.key());
			const auto direction = fitting_text(it.value(), "direction", path + ".services." + it.key());
			if (kind != "power" && kind != "coolant" && kind != "information")
				fitting_error(path + ".services." + it.key() + ".kind", "unsupported service kind");
			if (direction != "input" && direction != "output")
				fitting_error(path + ".services." + it.key(), "unsupported endpoint direction");
			fitting_text(it.value(), "target", path + ".services." + it.key());
			if (kind == "power")
				fitting_reference(it.value(), "target", resources.store_ids, path + ".services." + it.key());
			if (kind == "coolant")
				fitting_reference(it.value(), "target",
				                  direction == "output" ? resources.coolant_source_ids : resources.store_ids,
				                  path + ".services." + it.key());
			if (kind == "information")
			{
				const auto target = fitting_text(it.value(), "target", path + ".services." + it.key());
				const auto matches =
				    static_cast<int>(systems.contains(target)) + static_cast<int>(console_ids.contains(target));
				if (matches != 1)
					fitting_error(path + ".services." + it.key() + ".target",
					              "information endpoint must resolve to one fitted System or Console");
			}
		}
	}
	if (!entity.contains("presets"))
	{
		if (entity.contains("default_preset"))
			fitting_error(path + ".default_preset", "default has no preset catalogue");
		return;
	}
	const auto &presets = entity.at("presets");
	if (!presets.is_array() || presets.empty() || presets.size() > 64)
		fitting_error(path + ".presets", "expected 1..64 authored presets");
	std::set<std::string> preset_ids;
	for (const auto &preset : presets)
	{
		const auto id = fitting_text(preset, "id", path + ".presets");
		const auto at = path + ".presets." + id;
		if (!valid_identifier(id) || !preset_ids.insert(id).second)
			fitting_error(at, "invalid/duplicate preset identity");
		if (!preset.contains("consoles") || !preset.at("consoles").is_object() || preset.at("consoles").empty())
			fitting_error(at + ".consoles", "expected named Console configurations");
		for (auto row = preset.at("consoles").begin(); row != preset.at("consoles").end(); ++row)
		{
			const auto console = console_ids.find(row.key());
			const auto where = at + ".consoles." + row.key();
			if (console == console_ids.end() || !row.value().is_object())
				fitting_error(where, "unknown Console or invalid configuration");
			std::set<std::string> assigned, delegated;
			for (const char *field : {"direct", "delegated"})
			{
				if (!row.value().contains(field) || !row.value().at(field).is_array())
					fitting_error(where + "." + field, "expected explicit System list");
				for (const auto &value : row.value().at(field))
				{
					if (!value.is_string())
						fitting_error(where + "." + field, "expected fitted System identity");
					const auto name = value.get<std::string>();
					if (!owners.contains(name) || owners.at(name) != row.key() || !assigned.insert(name).second)
						fitting_error(where + "." + field, "unknown, repeated or foreign System '" + name + "'");
					if (std::string_view(field) == "delegated")
					{
						if (systems.at(name)->controller.empty())
							fitting_error(where + ".delegated", "System '" + name + "' has no ordinary controller");
						delegated.insert(name);
					}
				}
			}
			if (assigned.size() != console->second->controlled_systems.size())
				fitting_error(where, "direct and delegated lists must cover every owned System exactly once");
			const auto intents = row.value().value("summary_intents", Json::array());
			if (!intents.is_array() || intents.size() > 256)
				fitting_error(where + ".summary_intents", "expected bounded summary control list");
			std::set<std::pair<std::string, std::string>> summaries;
			for (const auto &intent : intents)
			{
				const auto system = fitting_text(intent, "system", where + ".summary_intents"),
				           channel = fitting_text(intent, "channel", where + ".summary_intents");
				const auto control = where + ".summary_intents." + system + "." + channel;
				if (!delegated.contains(system) || !valid_identifier(channel) || channel == "destination.x" ||
				    channel == "destination.y" || !summaries.emplace(system, channel).second)
					fitting_error(control, "summary must uniquely address an owned delegated System");
				const auto kind = std::string(operating_system_name(systems.at(system)->kind));
				const bool supported = (kind == "generator" && channel == "output") ||
				                       (kind == "distribution" && (channel == "power" || channel == "allocation")) ||
				                       (kind == "coolant" && (channel == "coolant" || channel == "cooling")) ||
				                       (kind == "storage" && (channel == "charge" || channel == "discharge"));
				const auto channels = gameplay_summary_channels(catalog, entity_id, system);
				if (std::find(channels.begin(), channels.end(), channel) == channels.end())
					fitting_error(control,
					              "channel needs a delegated controller summary binding or standard capability");
				if (!intent.contains("levels") || !intent.at("levels").is_array() || intent.at("levels").empty() ||
				    intent.at("levels").size() > 16)
					fitting_error(control + ".levels", "expected 1..16 bounded numeric choices");
				std::set<ResourceAmount> levels;
				ResourceAmount maximum_level = resource_limit;
				const auto &spec = entity.at("systems").at(system);
				if (supported && kind == "generator")
					maximum_level = 1000;
				else if (supported && kind == "coolant")
					maximum_level =
					    resources.cooling_links
					        .at(fitting_reference(spec, "cooling_link", resources.cooling_link_ids, control))
					        .capacity_rate;
				else if (supported && kind == "storage")
				{
					const auto &store =
					    resources.stores.at(fitting_reference(spec, "power_store", resources.store_ids, control));
					maximum_level = channel == "charge" ? store.charge_rate : store.discharge_rate;
				}
				for (const auto &value : intent.at("levels"))
				{
					if (!value.is_number_integer() || value.is_boolean() || value < 0 || value > resource_limit)
						fitting_error(control + ".levels", "invalid numeric level");
					const auto number = value.get<ResourceAmount>();
					if (number > maximum_level || !levels.insert(number).second)
						fitting_error(control + ".levels", "duplicate or out-of-range level");
				}
				if (!intent.contains("default") || !levels.contains(fitting_amount(intent, "default", control)))
					fitting_error(control + ".default", "must name an authored level");
				if (intent.contains("level_labels"))
				{
					if (!intent.at("level_labels").is_array() || intent.at("level_labels").size() != levels.size())
						fitting_error(control + ".level_labels", "labels must match level choices");
					for (const auto &label : intent.at("level_labels"))
						if (!label.is_string() || label.get<std::string>().empty())
							fitting_error(control + ".level_labels", "expected localized label keys");
				}
			}
		}
	}
	if (!entity.contains("default_preset") || !entity.at("default_preset").is_string() ||
	    !preset_ids.contains(entity.at("default_preset").get<std::string>()))
		fitting_error(path + ".default_preset", "must select a declared preset");
}
} // namespace phoenix
