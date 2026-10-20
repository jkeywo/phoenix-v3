module;
#include <set>
#include <algorithm>
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
Bytes export_fleet_setup(const HostAccess &source, const GameSetup &selection, const std::string &room,
                         const std::map<std::string, std::string> &participants)
{
	const auto &catalog = source.catalog();
	if (!valid_identifier(room) || !catalog.maps.contains(selection.mission))
		throw std::runtime_error("Select a verified mission and valid fleet room");
	const auto &slots = catalog.maps.at(selection.mission).at("ship_slots");
	if (slots.empty() || slots.size() > 16)
		throw std::runtime_error("A fixed fleet needs between one and sixteen authored ship slots");
	GameSetup game{selection.mission, {}, selection.local_slot, selection.local_console};
	game.console_profiles = selection.console_profiles;
	game.gm_host = selection.gm_host;
	Json members = Json::object();
	std::string creator;
	if (selection.gm_host)
	{
		if (!selection.local_slot.empty() || !selection.local_console.empty())
			throw std::runtime_error("A GM host cannot occupy a ship slot");
		for (const auto &[server, slot] : participants)
			if (slot.empty())
			{
				if (!creator.empty())
					throw std::runtime_error("Only one GM coordinator is supported");
				creator = server;
				members[server] = "";
			}
		if (creator.empty())
			throw std::runtime_error("A GM host needs a coordinator identity");
	}
	unsigned number = 0;
	for (const auto &slot : slots)
	{
		const std::string slot_id = slot.at("id");
		if (!selection.gm_host && !participants.empty() && !selection.ships.contains(slot_id) &&
		    !slot.value("mandatory", false))
			continue;
		std::string chosen;
		if (selection.ships.contains(slot_id))
			chosen = selection.ships.at(slot_id);
		else if (slot.contains("default_ship") || selection.gm_host ||
		         (!participants.empty() && slot.value("mandatory", false)))
			chosen = slot.value("default_ship", slot.at("allowed_ships").at(0).get<std::string>());
		else
			for (const auto &allowed : slot.at("allowed_ships"))
			{
				const auto &entity = catalog.entities.at(allowed.get<std::string>());
				const auto systems = resolve_systems(catalog, allowed.get<std::string>());
				const auto has = [&](OperatingSystem kind)
				{
					return std::any_of(systems.begin(), systems.end(),
					                   [&](const auto &s)
					                   {
						                   return s.kind == kind;
					                   });
				};
				if ((entity.contains("console") || entity.contains("stations")) && has(OperatingSystem::Engine) &&
				    has(OperatingSystem::Steering))
				{
					chosen = allowed;
					break;
				}
			}
		if (chosen.empty())
			throw std::runtime_error(
			    "Every fleet ship slot needs an allowed ship with an authored operating console: " + slot_id);
		const bool controlled = participants.empty() || std::any_of(participants.begin(), participants.end(),
		                                                            [&](const auto &p)
		                                                            {
			                                                            return p.second == slot_id;
		                                                            });
		if (controlled && !catalog.entities.at(chosen).contains("console") &&
		    !catalog.entities.at(chosen).contains("stations"))
			throw std::runtime_error("The selected fleet ship needs an authored operating console: " + slot_id);
		game.ships[slot_id] = chosen;
		if (participants.empty())
		{
			const auto server = "ship" + std::to_string(++number);
			members[server] = slot_id;
			if (slot_id == game.local_slot)
				creator = server;
		}
		else
			for (const auto &[server, owned] : participants)
				if (owned == slot_id)
				{
					members[server] = slot_id;
					if (slot_id == game.local_slot)
						creator = server;
				}
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
		if (participants.empty() || std::any_of(participants.begin(), participants.end(),
		                                        [&](const auto &p)
		                                        {
			                                        return p.second == id;
		                                        }))
			for (const auto &seat : resolve_consoles(catalog, ship))
				if (!seat.human_seeking && !candidate->authoritative_world().can_claim(
				                               console++, *candidate->authoritative_world().ship_for_slot(id), seat.id))
					throw std::runtime_error("A selected fleet ship cannot admit its authored console: " + id + "/" +
					                         seat.id);
	auto container = candidate->save_container();
	container.manifest["format"] = "phoenix-fleet-setup";
	container.files["fleet.json"] = json_bytes(Json{{"schema", 1},
	                                                {"room", room},
	                                                {"session", random_token()},
	                                                {"key", random_token()},
	                                                {"members", members},
	                                                {"gm_host", selection.gm_host},
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
		if (valid_identifier(server) && slot.is_string() && slot.get<std::string>().empty() &&
		    server == plan.at("creator").get<std::string>() && plan.value("gm_host", false))
			continue;
		if (!valid_identifier(server) || !slot.is_string() || !slots.insert(slot.get<std::string>()).second ||
		    !data.at("ships").contains(slot.get<std::string>()))
			throw std::runtime_error("Each fixed server must own a distinct selected ship slot");
	}
	// Mandatory AI-only slots have no server or quorum vote.
	data["local_slot"] = plan.at("members").at(self);
	data["gm_host"] = data.at("local_slot") == "";
	data["local_console"] = "";
	container.files["session.json"] = json_bytes(data);
	container.manifest["format"] = "phoenix-runtime-save";
	auto world = make_host_world();
	world->restore_container(container);
	world->resume_server();
	return {std::move(plan), self, std::move(world), archive};
}
Bytes save_fleet_recovery(const HostAccess &source, const Json &plan)
{
	auto container = source.save_container();
	container.manifest["format"] = "phoenix-fleet-setup";
	// Portable recovery starts a fresh epoch. Never copy peer hard state or credentials.
	container.files["fleet.json"] = json_bytes(Json{{"schema", 1},
	                                                {"room", plan.at("room")},
	                                                {"session", random_token()},
	                                                {"key", random_token()},
	                                                {"members", plan.at("members")},
	                                                {"creator", plan.at("creator")},
	                                                {"gm_host", plan.value("gm_host", false)},
	                                                {"recovery", true}});
	return write_pack(container);
}
} // namespace phoenix
