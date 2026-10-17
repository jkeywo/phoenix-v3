module;
#include <cstdint>
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
std::vector<ConsoleSpec> resolve_consoles(const Catalog &catalog, const std::string &id)
{
	const auto &entity = catalog.entities.at(id);
	const auto fail = [&](const std::string &reason)
	{
		throw std::runtime_error("entities." + id + ".stations: " + reason);
	};
	SystemMask fitted = 0;
	for (auto system : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
		if (entity.at("systems").contains(std::string(operating_system_name(system))))
			fitted |= system_bit(system);
	if (!entity.contains("stations"))
		return {{"main", "main", entity.value("console", std::string{}), fitted, fitted}};
	if (entity.contains("console") || entity.contains("controller"))
		fail("explicit Stations cannot also use a legacy console/controller");
	if (!entity["stations"].is_array() || entity["stations"].empty() || entity["stations"].size() > 32)
		fail("expected 1..32 Stations");
	std::vector<ConsoleSpec> result;
	std::set<std::string> stations, consoles;
	SystemMask owned = 0;
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
	const auto mask = [&](const Json &values)
	{
		if (!values.is_array())
			fail("System bindings must be arrays");
		SystemMask bits = 0;
		for (const auto &value : values)
		{
			SystemMask bit = 0;
			for (auto system : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
				if (value == std::string(operating_system_name(system)))
					bit = system_bit(system);
			if (!bit || !(fitted & bit) || (bits & bit))
				fail("unknown, unfitted or repeated System binding");
			bits |= bit;
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
			spec.controls = mask(console.at("systems"));
			spec.feeds = spec.controls | mask(console.value("feeds", Json::array()));
			if (owned & spec.controls)
				fail("a System has more than one controlling Console");
			owned |= spec.controls;
			for (const auto &widget : catalog.consoles.at(spec.definition).at("widgets"))
			{
				const auto kind = widget.at("kind").get<std::string>();
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
	if (owned != fitted)
		fail("each fitted System requires one controlling Console");
	return result;
}
} // namespace phoenix
