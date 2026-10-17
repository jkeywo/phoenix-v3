module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.content;
namespace phoenix
{
Pack console_assets(const Catalog &catalog, const std::string &ship)
{
	const auto &source = catalog.entities.at(ship);
	PackDraft draft;
	const std::string pack_id = "console-assets";
	draft.manifest = {{"schema", 1}, {"id", pack_id}, {"format", "phoenix-console-assets"}};
	Json entity{{"id", ship}, {"systems", Json::object()}};
	entity["render"] = {{"mode", source.at("render").at("mode")},
	                    {"scale", source.at("render").at("scale")},
	                    {"images", Json::array()}};
	for (const auto &image : source.at("render").at("images"))
	{
		const auto &bytes = catalog.assets.at(image.get<std::string>());
		const auto path = "assets/" + sha256(bytes) + ".png";
		draft.files[path] = bytes;
		entity["render"]["images"].push_back(pack_id + ":" + path);
	}
	const auto &systems = source.at("systems");
	if (systems.contains("engine"))
		entity["systems"]["engine"] = {{"max_speed_mm_s", systems["engine"]["max_speed_mm_s"]}};
	if (systems.contains("steering"))
		entity["systems"]["steering"] = Json::object();
	if (systems.contains("radar"))
		entity["systems"]["radar"] = {{"range_mm", systems["radar"]["range_mm"]}};
	Json consoles = Json::array(), stations = Json::array();
	std::set<std::string> included;
	for (const auto &seat : resolve_consoles(catalog, ship))
	{
		Json controls = Json::array(), feeds = Json::array();
		for (auto system : {OperatingSystem::Engine, OperatingSystem::Steering, OperatingSystem::Radar})
		{
			if (seat.controls & system_bit(system))
				controls.push_back(std::string(operating_system_name(system)));
			if (seat.feeds & system_bit(system))
				feeds.push_back(std::string(operating_system_name(system)));
		}
		// Copy the public fields, never arbitrary authored metadata.
		auto station = std::find_if(stations.begin(), stations.end(),
		                            [&](const Json &value)
		                            {
			                            return value.at("id") == seat.station;
		                            });
		if (station == stations.end())
		{
			stations.push_back({{"id", seat.station}, {"consoles", Json::array()}});
			station = stations.end() - 1;
		}
		(*station)["consoles"].push_back(
		    {{"id", seat.id}, {"definition", seat.definition}, {"systems", controls}, {"feeds", feeds}});
		if (included.insert(seat.definition).second)
		{
			Json layout{{"id", seat.definition}, {"widgets", Json::array()}};
			for (const auto &widget : catalog.consoles.at(seat.definition).at("widgets"))
			{
				Json copy;
				for (const auto *field : {"id", "kind", "label", "binding", "x", "y", "width", "height"})
					copy[field] = widget.at(field);
				layout["widgets"].push_back(std::move(copy));
			}
			consoles.push_back(std::move(layout));
		}
	}
	entity["stations"] = stations;
	draft.files["definitions.json"] =
	    json_bytes(Json{{"schema", 2}, {"entities", Json::array({entity})}, {"consoles", consoles}});
	auto pack = read_pack(write_pack(draft));
	resolve_packs({pack});
	return pack;
}
} // namespace phoenix
