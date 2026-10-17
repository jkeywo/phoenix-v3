module;
#include <set>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.fleet_setup;
import phoenix.wire;

namespace phoenix
{
Bytes export_fleet_setup(const HostAccess &source, const GameSetup &selection, const std::string &room)
{
	const auto &catalog = source.catalog();
	if (!valid_identifier(room) || !catalog.maps.contains(selection.mission))
		throw std::runtime_error("Select a verified mission and valid fleet room");
	const auto &slots = catalog.maps.at(selection.mission).at("ship_slots");
	if (slots.empty() || slots.size() > 16)
		throw std::runtime_error("A fixed fleet needs between one and sixteen authored ship slots");
	GameSetup game{selection.mission, {}, selection.local_slot};
	Json members = Json::object();
	std::string creator;
	unsigned number = 0;
	for (const auto &slot : slots)
	{
		const std::string slot_id = slot.at("id");
		std::string chosen;
		if (selection.ships.contains(slot_id))
			chosen = selection.ships.at(slot_id);
		else
			for (const auto &allowed : slot.at("allowed_ships"))
			{
				const auto &entity = catalog.entities.at(allowed.get<std::string>());
				const auto &systems = entity.at("systems");
				if (entity.contains("console") && systems.contains("engine") && systems.contains("steering"))
				{
					chosen = allowed;
					break;
				}
			}
		if (chosen.empty())
			throw std::runtime_error(
			    "Every fleet ship slot needs an allowed ship with an authored operating console: " + slot_id);
		if (!catalog.entities.at(chosen).contains("console"))
			throw std::runtime_error("The selected fleet ship needs an authored operating console: " + slot_id);
		game.ships[slot_id] = chosen;
		const auto server = "ship" + std::to_string(++number);
		members[server] = slot_id;
		if (slot_id == game.local_slot)
			creator = server;
	}
	if (creator.empty())
		throw std::runtime_error("Choose the local ship slot before creating the fleet setup");
	auto candidate = make_host_world();
	for (const auto &pack : source.content_packs())
		candidate->add_verified_pack(pack);
	candidate->start_server(game);
	// Validate each selected ship through the same admission used by a real station.
	std::uint64_t console = 1;
	for (const auto &[id, ship] : game.ships)
		if (!candidate->authoritative_world().can_claim(console++, *candidate->authoritative_world().ship_for_slot(id)))
			throw std::runtime_error("A selected fleet ship cannot admit its authored console: " + id);
	auto container = candidate->save_container();
	container.manifest["format"] = "phoenix-fleet-setup";
	container.files["fleet.json"] = json_bytes(Json{{"schema", 1},
	                                                {"room", room},
	                                                {"session", random_token()},
	                                                {"key", random_token()},
	                                                {"members", members},
	                                                {"creator", creator}});
	return write_pack(container);
}
PreparedFleet read_fleet_setup(const Bytes &archive, const std::string &member)
{
	auto container = read_pack(archive).draft();
	if (container.manifest.value("format", std::string{}) != "phoenix-fleet-setup")
		throw std::runtime_error("Select a private Phoenix fleet setup ZIP");
	if (container.files.at("fleet.json").size() > 65536)
		throw std::runtime_error("Fleet setup metadata exceeds bounds");
	auto plan = parse_json(container.files.at("fleet.json"), "fleet.json");
	if (plan.at("schema") != 1 || !valid_hex_token(plan.at("session").get<std::string>()) ||
	    !valid_hex_token(plan.at("key").get<std::string>()) || !valid_identifier(plan.at("room").get<std::string>()) ||
	    !plan.at("members").is_object() || plan.at("members").empty() || plan.at("members").size() > 16)
		throw std::runtime_error("Invalid fixed fleet roster, room or credential");
	const std::string self = member.empty() ? plan.at("creator").get<std::string>() : member;
	if (!plan.at("members").contains(self))
		throw std::runtime_error("The selected server is outside this fleet's fixed roster");
	auto data = parse_json(container.files.at("session.json"), "session.json");
	std::set<std::string> slots;
	for (const auto &[server, slot] : plan.at("members").items())
	{
		if (!valid_identifier(server) || !slot.is_string() || !slots.insert(slot.get<std::string>()).second ||
		    !data.at("ships").contains(slot.get<std::string>()))
			throw std::runtime_error("Each fixed server must own a distinct selected ship slot");
	}
	if (slots.size() != data.at("ships").size())
		throw std::runtime_error("Fleet roster and selected ships differ");
	data["local_slot"] = plan.at("members").at(self);
	container.files["session.json"] = json_bytes(data);
	container.manifest["format"] = "phoenix-runtime-save";
	auto world = make_host_world();
	world->restore_container(container);
	world->resume_server();
	return {std::move(plan), self, std::move(world), archive};
}
} // namespace phoenix
