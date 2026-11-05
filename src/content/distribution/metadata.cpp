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
				                          {"briefing", mission_briefing(map, slot.at("id"), ship_id)}});
			}
			scenario["slots"].push_back(std::move(entry));
		}
		result["scenarios"].push_back(std::move(scenario));
	}
	return result;
}

std::string mission_briefing(const Json &map, const std::string &slot, const std::string &ship)
{
	if (const auto slots = map.find("slot_briefings"); slots != map.end())
		if (const auto choices = slots->find(slot); choices != slots->end())
			if (const auto briefing = choices->find(ship); briefing != choices->end())
				return briefing->get<std::string>();
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
			std::set<std::string> slot_ids;
			for (const auto &slot : scene.at("slots"))
			{
				const auto slot_id = text(slot.at("id"));
				if (!slot_ids.insert(slot_id).second)
					throw std::runtime_error("Duplicate menu slot");
				Json ships = Json::array();
				std::set<std::string> ship_ids;
				if (!slot.at("ships").is_array() || slot.at("ships").empty() || slot.at("ships").size() > 256)
					throw std::runtime_error("Invalid menu ships");
				for (const auto &ship : slot.at("ships"))
				{
					const auto ship_id = text(ship.at("id"));
					if (!ship_ids.insert(ship_id).second)
						throw std::runtime_error("Duplicate menu ship in slot");
					ships.push_back(ship_id);
					result.entities[ship_id] = {{"id", ship_id}, {"name", text(ship.at("name"))}};
					if (ship.contains("briefing"))
					{
						const auto &briefing = ship.at("briefing");
						if (!briefing.is_string() || briefing.get_ref<const std::string &>().size() > 4096)
							throw std::runtime_error("Invalid menu briefing");
						map["slot_briefings"][slot_id][ship_id] = briefing;
					}
				}
				auto default_ship = slot.value("default_ship", std::string{});
				if (default_ship.empty())
					default_ship = ships.front().get<std::string>();
				if (!ship_ids.contains(default_ship))
					throw std::runtime_error("Menu slot default must name an allowed ship");
				map["ship_slots"].push_back({{"id", slot_id},
				                             {"allowed_ships", std::move(ships)},
				                             {"mandatory", slot.value("mandatory", false)},
				                             {"default_ship", std::move(default_ship)}});
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
std::string ship_client_location(const Json &descriptor, const std::map<std::string, std::string> &sources)
{
	if (!descriptor.is_object() || !descriptor.contains("sources") || !descriptor.at("sources").is_array() ||
	    descriptor.at("sources").empty() || descriptor.at("sources").size() > 32 || !descriptor.contains("ship") ||
	    !descriptor.at("ship").is_string())
		return {};
	const auto &last = descriptor.at("sources").back();
	if (!last.is_array() || last.size() != 2 || !last[1].is_string())
		return {};
	const auto hash = last[1].get<std::string>();
	const auto source = sources.find(hash);
	if (source == sources.end() || source->second.empty())
		return {};
	const auto split = source->second.find_first_of("?#");
	return source->second.substr(0, split) + ".clients/" + sha256(json_bytes(descriptor.at("ship"))) + ".dat" +
	       (split == std::string::npos ? "" : source->second.substr(split));
}
std::string ship_client_location(const Catalog &catalog, const std::string &ship,
                                 const std::map<std::string, std::string> &sources)
{
	return ship_client_location(Json{{"sources", catalog.ordered_packs}, {"ship", ship}}, sources);
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
	if (filename.ends_with(".mission.dat"))
	{
		// The lobby archive carries only the public projection. Its launcher
		// metadata names and hashes the authoritative mission archive.
		menu["mission_file"] = filename;
		menu["lobby_version"] = 1;
		menu["lobby_ships"] = Json::object();
		for (const auto &ship : menu.at("ships"))
		{
			const auto id = ship.at("id").get<std::string>();
			auto &entry = menu["lobby_ships"][id];
			entry = {{"name", ship.at("name")}, {"consoles", Json::object()}};
			const auto layout = compile_controllable_layout(catalog, id);
			for (const auto &seat : resolve_consoles(catalog, id))
			{
				if (seat.human_seeking)
					continue;
				const auto &definition = catalog.consoles.at(seat.definition);
				Json row{{"name", definition.value("name", seat.id)},
				         {"description", definition.value("description", std::string{})},
				         {"help", definition.value("help", Json::object())},
				         {"profiles", Json::array()}};
				if (catalog.entities.at(id).contains("control_layout") && !definition.value("command", false))
				{
					const auto fixed = layout.fixed_profiles.find(seat.id);
					for (std::size_t i = 0; i < layout.profiles.size(); ++i)
						if (fixed == layout.fixed_profiles.end() || fixed->second == i)
							row["profiles"].push_back(
							    {{"id", layout.profiles[i].id}, {"stars", layout.profiles[i].stars}});
					const auto selected = layout.default_profiles.find(seat.id);
					row["default_profile"] =
					    layout.profiles
					        .at(selected == layout.default_profiles.end() ? layout.default_profile : selected->second)
					        .id;
				}
				entry["consoles"][seat.id] = std::move(row);
			}
		}
		PackDraft lobby;
		lobby.manifest = {{"schema", 1}, {"id", menu.at("id")}, {"format", "phoenix-lobby-metadata"}};
		lobby.files["definitions.json"] = json_bytes(Json::object());
		lobby.files["launcher.json"] = json_bytes(menu);
		files[filename.substr(0, filename.size() - std::string_view(".mission.dat").size()) + ".lobby.dat"] =
		    write_pack(lobby, PackEncoding::Dat);
	}
	return files;
}
} // namespace phoenix
