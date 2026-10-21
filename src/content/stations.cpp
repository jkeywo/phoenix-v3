module;
#include <cstdint>
#include <algorithm>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

module phoenix.content;
namespace phoenix
{
std::vector<FittedSystemSpec> resolve_systems(const Catalog &catalog, const std::string &id)
{
	const auto &entity = catalog.entities.at(id);
	const auto &systems = entity.at("systems");
	if (!systems.is_object() || systems.size() > 256)
		throw std::runtime_error("entities." + id + ".systems: expected at most 256 fitted Systems");
	std::vector<FittedSystemSpec> result;
	for (auto it = systems.begin(); it != systems.end(); ++it)
	{
		const auto path = "entities." + id + ".systems." + it.key();
		if (it.key().empty() || it.key().size() > 128 || !it.value().is_object() ||
		    it.key().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
		        std::string::npos)
			throw std::runtime_error(path + ": invalid System identity/definition");
		const auto kind = it.value().value("kind", it.key());
		std::optional<OperatingSystem> type;
		for (auto candidate :
		     {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar, OperatingSystem::Generator,
		      OperatingSystem::Storage, OperatingSystem::Distribution, OperatingSystem::Coolant,
		      OperatingSystem::Lateral, OperatingSystem::Boost, OperatingSystem::Impulse, OperatingSystem::Weapon,
		      OperatingSystem::Turret, OperatingSystem::Magazine, OperatingSystem::RepairTeam, OperatingSystem::Shield,
		      OperatingSystem::Comms})
			if (kind == operating_system_name(candidate))
				type = candidate;
		if (!type)
			throw std::runtime_error(path + ".kind: unsupported capability");
		FittedSystemSpec spec{it.key(), *type};
		if (*type == OperatingSystem::Engine)
		{
			spec.maximum = it.value().at("max_speed_mm_s");
			spec.response = it.value().value("acceleration_mm_s2", std::int64_t{0});
		}
		else if (*type == OperatingSystem::Steering)
			spec.response = it.value().value("turn_mdeg_s", std::int64_t{0});
		else if (*type == OperatingSystem::Radar || *type == OperatingSystem::Comms)
			spec.maximum = it.value().at("range_mm");
		else
		{
			spec.maximum = it.value().value("maximum", std::int64_t{1000});
			if (*type == OperatingSystem::Lateral || *type == OperatingSystem::Boost ||
			    *type == OperatingSystem::Impulse)
				spec.maximum =
				    *type == OperatingSystem::Lateral
				        ? it.value().value("lateral_speed_mm_s", it.value().value("max_speed_mm_s", std::int64_t{0}))
				        : it.value().value("max_speed_mm_s", std::int64_t{0});
			if (catalog.resource_definitions.contains(id))
			{
				const auto &network = catalog.resource_definitions.at(id);
				const auto reference = [&](const char *key)
				{
					return it.value().value(key, std::string{});
				};
				if (*type == OperatingSystem::Generator && network.generator_ids.contains(reference("generator")))
					spec.maximum = network.generators.at(network.generator_ids.at(reference("generator"))).maximum_rate;
				if (*type == OperatingSystem::Storage && network.store_ids.contains(reference("power_store")))
				{
					const auto &store = network.stores.at(network.store_ids.at(reference("power_store")));
					spec.maximum = std::max({store.capacity, store.charge_rate, store.discharge_rate});
				}
				if (*type == OperatingSystem::Distribution && network.group_ids.contains(reference("power_group")))
					spec.maximum = network.groups.at(network.group_ids.at(reference("power_group"))).budget_rate;
				if (*type == OperatingSystem::Coolant && network.cooling_link_ids.contains(reference("cooling_link")))
					spec.maximum =
					    network.cooling_links.at(network.cooling_link_ids.at(reference("cooling_link"))).capacity_rate;
			}
		}
		if (entity.contains("controllers") && entity.at("controllers").contains(it.key()))
			spec.controller = entity.at("controllers").at(it.key()).get<std::string>();
		result.push_back(std::move(spec));
	}
	return result;
}
std::vector<ConsoleSpec> resolve_consoles(const Catalog &catalog, const std::string &id)
{
	const auto &entity = catalog.entities.at(id);
	const auto fail = [&](const std::string &reason)
	{
		throw std::runtime_error("entities." + id + ".stations: " + reason);
	};
	SystemMask fitted = 0;
	const auto instances = resolve_systems(catalog, id);
	std::map<std::string, OperatingSystem> types;
	for (const auto &system : instances)
	{
		fitted |= system_bit(system.kind);
		types.emplace(system.id, system.kind);
	}
	if (!entity.contains("stations"))
	{
		ConsoleSpec seat{"main", "main", entity.value("console", std::string{}), fitted, fitted};
		if (!seat.definition.empty() && catalog.consoles.contains(seat.definition))
		{
			seat.human_seeking =
			    catalog.consoles.at(seat.definition)
			        .value("human_seeking", catalog.consoles.at(seat.definition).value("command", false));
			seat.human_preferences = {"main"};
		}
		for (const auto &system : instances)
		{
			seat.controlled_systems.push_back(system.id);
			seat.feed_systems.push_back(system.id);
		}
		bool instance_form = false;
		for (const auto &system : instances)
			instance_form |=
			    system.id != operating_system_name(system.kind) || entity.at("systems").at(system.id).contains("kind");
		if (!seat.definition.empty() && catalog.consoles.contains(seat.definition))
			for (const auto &widget : catalog.consoles.at(seat.definition).at("widgets"))
			{
				if (widget.contains("controller"))
				{
					if (!catalog.presentation && !entity.contains("control_layout"))
						fail("Controller widget requires an explicit control layout");
					continue;
				}
				if (!instance_form && !widget.contains("system"))
					continue;
				const auto kind = widget.at("kind").get<std::string>();
				if (kind != "throttle" && kind != "heading" && kind != "radar")
					continue;
				const auto type = kind == "throttle"  ? OperatingSystem::Engine
				                  : kind == "heading" ? OperatingSystem::Steering
				                                      : OperatingSystem::Radar;
				const auto requested = widget.value("system", std::string{});
				int count = 0;
				for (const auto &instance : instances)
					if (instance.kind == type && (requested.empty() || instance.id == requested))
						++count;
				if (count != 1)
					fail(kind + " widget requires an explicit or uniquely resolving System");
			}
		return {seat};
	}
	if (entity.contains("console") || entity.contains("controller"))
		fail("explicit Stations cannot also use a legacy console/controller");
	if (!entity["stations"].is_array() || entity["stations"].empty() || entity["stations"].size() > 32)
		fail("expected 1..32 Stations");
	std::vector<ConsoleSpec> result;
	std::set<std::string> stations, consoles;
	std::set<std::string> owned;
	const auto name = [&](const Json &value, const char *field)
	{
		if (!value.contains(field) || !value[field].is_string())
			fail(std::string("missing ") + field);
		auto text = value.at(field).get<std::string>();
		if (text.empty() || text.size() > 128 ||
		    text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
		        std::string::npos)
			fail(std::string("invalid ") + field);
		return text;
	};
	const auto mask = [&](const Json &values, std::vector<std::string> &ids)
	{
		if (!values.is_array())
			fail("System bindings must be arrays");
		SystemMask bits = 0;
		std::set<std::string> seen;
		for (const auto &value : values)
		{
			if (!value.is_string() || !types.contains(value.get<std::string>()) ||
			    !seen.insert(value.get<std::string>()).second)
				fail("unknown, unfitted or repeated System binding");
			ids.push_back(value.get<std::string>());
			bits |= system_bit(types.at(ids.back()));
		}
		return bits;
	};
	for (const auto &station : entity["stations"])
	{
		auto station_id = name(station, "id");
		if (!stations.insert(station_id).second || !station.contains("consoles") || !station["consoles"].is_array() ||
		    station["consoles"].empty())
			fail("duplicate Station or missing Consoles");
		for (const auto &console : station["consoles"])
		{
			ConsoleSpec spec{name(console, "id"), station_id, name(console, "definition")};
			if (!consoles.insert(spec.id).second || result.size() >= 32)
				fail("Console identities must be unique within 32 seats per ship");
			if (!catalog.consoles.contains(spec.definition))
				fail("unresolved Console definition");
			spec.human_seeking =
			    catalog.consoles.at(spec.definition)
			        .value("human_seeking", catalog.consoles.at(spec.definition).value("command", false));
			spec.human_preferences = console.value("human_preferences", std::vector<std::string>{});
			spec.controls = mask(console.value("systems", Json::array()), spec.controlled_systems);
			spec.feeds = spec.controls | mask(console.value("feeds", Json::array()), spec.feed_systems);
			for (const auto &system : spec.controlled_systems)
			{
				if (!owned.insert(system).second && !entity.contains("control_layout"))
					fail("a System has more than one controlling Console");
				if (std::find(spec.feed_systems.begin(), spec.feed_systems.end(), system) == spec.feed_systems.end())
					spec.feed_systems.push_back(system);
			}
			for (const auto &widget : catalog.consoles.at(spec.definition).at("widgets"))
			{
				if (widget.contains("controller"))
				{
					if (!catalog.presentation && !entity.contains("control_layout"))
						fail("Controller widget requires an explicit control layout");
					continue;
				}
				const auto kind = widget.at("kind").get<std::string>();
				if (kind == "throttle" || kind == "heading" || kind == "radar")
				{
					const auto type = kind == "throttle"  ? OperatingSystem::Engine
					                  : kind == "heading" ? OperatingSystem::Steering
					                                      : OperatingSystem::Radar;
					const auto &bindings = kind == "radar" ? spec.feed_systems : spec.controlled_systems;
					const auto requested = widget.value("system", std::string{});
					int matches = 0;
					for (const auto &key : bindings)
						if (types.at(key) == type && (requested.empty() || key == requested))
							++matches;
					if (matches != 1)
						fail(kind + " widget requires an explicit or uniquely resolving System");
				}
				if (kind == "throttle" && !(spec.controls & system_bit(OperatingSystem::Engine)))
					fail("throttle widget requires engine control");
				if (kind == "heading" && !(spec.controls & system_bit(OperatingSystem::Steering)))
					fail("heading widget requires steering control");
				if (kind == "radar" && !(spec.feeds & system_bit(OperatingSystem::Radar)))
					fail("radar widget requires an explicit feed");
			}
			result.push_back(std::move(spec));
		}
	}
	for (auto &spec : result)
	{
		std::set<std::string> seen;
		for (const auto &station : spec.human_preferences)
			if (!stations.contains(station) || !seen.insert(station).second)
				fail("unknown or repeated human-seeking station preference");
		if (spec.human_preferences.empty())
		{
			spec.human_preferences.push_back(spec.station);
			for (const auto &station : entity["stations"])
				if (station.at("id") != spec.station)
					spec.human_preferences.push_back(station.at("id"));
		}
	}
	if (owned.size() != instances.size() && !entity.contains("control_layout") && !catalog.presentation)
		fail("each fitted System requires one controlling Console");
	return result;
}
} // namespace phoenix
