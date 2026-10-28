module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

module phoenix.content.distribution;
import phoenix.content.verification;
import phoenix.content.ui;
import phoenix.content.assets;
import phoenix.content.controllers;

namespace phoenix
{
Json launcher_catalog(const Catalog &catalog)
{
	Json result{{"schema", 1}, {"scenarios", Json::array()}};
	result["theme"] = public_ui_theme(catalog);
	result["base"] = {{"id", catalog.base.id},
	                  {"hash", catalog.base.hash},
	                  {"render_mode", catalog.base.render_mode},
	                  {"interface_style", catalog.base.interface_style}};
	for (const auto &[id, map] : catalog.maps)
	{
		Json scenario{{"id", id}, {"name", map.value("name", id)}, {"slots", Json::array()}};
		for (const auto &slot : map.at("ship_slots"))
		{
			Json entry{
			    {"id", slot.at("id")},
			    {"mandatory", slot.value("mandatory", false)},
			    {"default_ship", slot.value("default_ship", slot.at("allowed_ships").front().get<std::string>())},
			    {"ships", Json::array()}};
			for (const auto &ship : slot.at("allowed_ships"))
			{
				const auto ship_id = ship.get<std::string>();
				entry["ships"].push_back({{"id", ship_id},
				                          {"name", catalog.entities.at(ship_id).value("name", ship_id)},
				                          {"briefing", mission_briefing(map, ship_id)}});
			}
			scenario["slots"].push_back(std::move(entry));
		}
		result["scenarios"].push_back(std::move(scenario));
	}
	return result;
}

std::string mission_briefing(const Json &map, const std::string &ship)
{
	const auto variants = map.value("ship_briefings", Json::object());
	return variants.value(ship, map.value("briefing_key", std::string{}));
}

Catalog menu_catalog(const std::vector<Json> &menus)
{
	Catalog result;
	const auto text = [](const Json &value)
	{
		if (!value.is_string() || value.get_ref<const std::string &>().empty() ||
		    value.get_ref<const std::string &>().size() > 256)
			throw std::runtime_error("Invalid menu text");
		return value.get<std::string>();
	};
	for (const auto &menu : menus)
	{
		if (menu.value("schema", 0) != 1 || !menu.at("scenarios").is_array() || menu.at("scenarios").size() > 1024)
			throw std::runtime_error("Invalid menu scenarios");
		for (const auto &scene : menu.at("scenarios"))
		{
			const auto id = text(scene.at("id"));
			Json map{{"id", id}, {"name", text(scene.at("name"))}, {"ship_slots", Json::array()}};
			if (!scene.at("slots").is_array() || scene.at("slots").size() > 64)
				throw std::runtime_error("Invalid menu slots");
			for (const auto &slot : scene.at("slots"))
			{
				Json ships = Json::array();
				if (!slot.at("ships").is_array() || slot.at("ships").empty() || slot.at("ships").size() > 256)
					throw std::runtime_error("Invalid menu ships");
				for (const auto &ship : slot.at("ships"))
				{
					const auto ship_id = text(ship.at("id"));
					ships.push_back(ship_id);
					result.entities[ship_id] = {{"id", ship_id}, {"name", text(ship.at("name"))}};
					if (ship.contains("briefing"))
					{
						const auto &briefing = ship.at("briefing");
						if (!briefing.is_string() || briefing.get_ref<const std::string &>().size() > 4096)
							throw std::runtime_error("Invalid menu briefing");
						map["ship_briefings"][ship_id] = briefing;
					}
				}
				map["ship_slots"].push_back({{"id", text(slot.at("id"))}, {"allowed_ships", ships}});
			}
			result.maps[id] = std::move(map);
		}
	}
	return result;
}

Json ship_client_descriptor(const Catalog &catalog, const std::string &ship, const std::string &hash,
                            const std::map<std::string, std::string> &urls)
{
	Json result{{"ship", ship}, {"hash", hash}, {"sources", catalog.ordered_packs}};
	if (!catalog.ordered_packs.empty())
	{
		const auto found = urls.find(catalog.ordered_packs.back().second);
		if (found != urls.end() && (found->second.starts_with("https://") || found->second.starts_with("http://")))
		{
			const auto split = found->second.find_first_of("?#");
			result["url"] = found->second.substr(0, split) + ".clients/" + sha256(json_bytes(ship)) + ".dat" +
			                (split == std::string::npos ? "" : found->second.substr(split));
		}
	}
	return result;
}

Json pack_menu(const Catalog &catalog, const std::vector<Pack> &ordered)
{
	if (ordered.empty())
		throw std::runtime_error("Menu requires a pack");
	const auto &pack = ordered.back();
	const auto &manifest = pack.manifest();
	auto result = launcher_catalog(catalog);
	result.erase("base");
	for (const auto *key : {"id", "name", "parent", "base", "overrides"})
		if (manifest.contains(key))
			result[key] = manifest.at(key);
	if (!manifest.contains("parent") && pack_dependencies(manifest).empty())
		result["base"] = {{"render_mode", catalog.base.render_mode}, {"interface_style", catalog.base.interface_style}};
	result["hash"] = pack.hash();
	result["dependencies"] = pack_dependencies(manifest);
	result["sources"] = catalog.ordered_packs;
	result["sidecar_version"] = 1;
	result["ships"] = Json::array();
	for (const auto &[id, entity] : catalog.entities)
		if ((entity.contains("stations") || entity.contains("console")) && !resolve_consoles(catalog, id).empty())
			result["ships"].push_back({{"id", id}, {"name", entity.value("name", id)}});
	const auto authored =
	    parse_json(pack.files().at("definitions.json"), "definitions.json").value("maps", Json::array());
	std::erase_if(result["scenarios"].get_ref<Json::array_t &>(),
	              [&](const auto &scene)
	              {
		              return std::none_of(authored.begin(), authored.end(),
		                                  [&](const auto &map)
		                                  {
			                                  return map.at("id") == scene.at("id");
		                                  });
	              });
	return result;
}

Json pack_menu(const std::vector<Pack> &ordered)
{
	if (ordered.empty())
		throw std::runtime_error("Menu requires a pack");
	return pack_menu(resolve_packs(ordered), ordered);
}

std::map<std::string, Bytes> pack_companions(const std::vector<Pack> &ordered, const std::string &filename)
{
	if (filename.empty() || filename.find_first_of("/\\:") != std::string::npos)
		throw std::runtime_error("Companion archive name must be a filename");
	const auto catalog = resolve_packs(ordered);
	auto menu = pack_menu(catalog, ordered);
	menu["clients"] = Json::array();
	std::map<std::string, Bytes> files;
	for (const auto &ship : menu.at("ships"))
	{
		const std::string id = ship.at("id");
		const auto client = [&]
		{
			try
			{
				return ship_client_assets(catalog, id);
			}
			catch (const std::exception &e)
			{
				throw std::runtime_error("Client bundle " + id + ": " + e.what());
			}
		}();
		const auto path = filename + ".clients/" + sha256(json_bytes(id)) + ".dat";
		files[path] = client.archive();
		menu["clients"].push_back(
		    {{"ship", id}, {"file", path}, {"hash", client.hash()}, {"sources", catalog.ordered_packs}});
	}
	files[filename + ".menu.json"] = json_bytes(menu);
	return files;
}
} // namespace phoenix
