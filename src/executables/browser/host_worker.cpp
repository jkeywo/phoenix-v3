#include <emscripten.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.network.lobby;
import phoenix.networking.projection;
import phoenix.content.scenarios;

#include "host_projection.hpp"

namespace
{
using namespace phoenix;
std::shared_ptr<SessionPort> session;
std::vector<Bytes> archives;
std::size_t archive_bytes = 0;
double accumulated = 0;
std::string reply;
Bytes exported_save;

template <class Operation> const char *respond(Operation operation)
{
	try
	{
		reply = Json{{"ok", true}, {"value", operation()}}.dump();
	}
	catch (const std::exception &error)
	{
		reply = Json{{"ok", false}, {"error", error.what()}}.dump();
	}
	return reply.c_str();
}

SessionPort &host()
{
	if (!session)
		throw std::runtime_error("Worker host has no lobby");
	return *session;
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_open(const char *request)
{
	return respond(
	    [&]
	    {
		    if (session)
			    throw std::runtime_error("Worker already owns a host");
		    const auto data = Json::parse(request);
		    const auto &menu = data.at("menu");
		    const auto &base = menu.at("base");
		    NetworkOptions options;
		    options.expected_base = {menu.at("id"), menu.at("hash"), base.value("render_mode", std::string("sprites")),
		                             base.value("interface_style", std::string("current"))};
		    options.pending_lobby_menu = menu;
		    options.link.url = data.value("service", std::string(default_network_service));
		    options.link.force_relay = data.value("force_relay", false);
		    options.player_name = data.value("player", std::string("Host"));
		    options.avatar = data.value("avatar", Bytes{});
		    options.profile = data.value("profile", std::string("default"));
		    options.link.room = data.value("room", std::string{});
		    options.link.ice_servers = data.value("ice", std::vector<std::string>{});
		    options.pack_urls = data.value("pack_urls", std::map<std::string, std::string>{});
		    if (const auto url = data.value("mission_url", std::string{}); !url.empty())
			    options.pack_urls[options.expected_base.hash] = url;
		    GameSetup setup;
		    setup.mission = data.at("scenario");
		    setup.gm_host = data.value("gm_host", false);
		    if (!setup.gm_host)
		    {
			    setup.local_slot = data.at("slot");
			    setup.ships.emplace(setup.local_slot, data.at("ship").get<std::string>());
		    }
		    auto candidate = make_game_lobby(std::move(options), data.value("offline", false));
		    candidate->prepare_lobby(setup);
		    session = std::move(candidate);
		    return Json(true);
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_archive(const std::uint8_t *data, std::size_t size)
{
	return respond(
	    [&]
	    {
		    (void)host();
		    if (!size || size > content_archive_limit || archives.size() >= 32 ||
		        size > 256 * 1024 * 1024 - archive_bytes)
			    throw std::runtime_error("Worker mission archives exceed their bounds");
		    archives.emplace_back(data, data + size);
		    archive_bytes += size;
		    return Json(true);
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_attach()
{
	return respond(
	    []
	    {
		    auto input = std::exchange(archives, {});
		    archive_bytes = 0;
		    host().replace_packs(input);
		    return Json(true);
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_worker_host_discard_archives()
{
	archives.clear();
	archive_bytes = 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_worker_host_close()
{
	session.reset();
	phoenix_worker_host_discard_archives();
	accumulated = 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_action(const char *action)
{
	return respond(
	    [&]
	    {
		    return Json(host().session_action(action));
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_save()
{
	return respond(
	    []
	    {
		    exported_save = host().save();
		    return Json(exported_save.size());
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE const std::uint8_t *phoenix_worker_host_save_bytes()
{
	return exported_save.data();
}

extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_worker_host_release_save()
{
	Bytes{}.swap(exported_save);
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_command(const char *request)
{
	return respond(
	    [&]
	    {
		    const auto data = Json::parse(request);
		    const auto op = data.at("op").get<std::string>();
		    auto &game = host();
		    if (op == "lobby")
			    return Json(game.lobby_command(data.at("value").get<LobbyCommand>()));
		    if (op == "profile")
			    return Json(game.set_player_profile(data.at("name"), data.at("avatar").get<Bytes>()));
		    if (op == "local-profile")
			    return Json(
			        game.set_local_player_profile(data.at("route"), data.at("name"), data.at("avatar").get<Bytes>()));
		    if (op == "claim")
			    return Json(game.claim_console(data.at("route"), data.at("address").get<ConsoleAddress>()));
		    if (op == "release")
			    return Json(game.release_console(data.at("route")));
		    if (op == "command")
			    return Json(
			        game.dispatch_console_command(data.at("route"), decode_operating_command(data.at("value"))));
		    throw std::runtime_error("Unknown browser host command");
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_catalog()
{
	return respond(
	    []
	    {
		    const auto &catalog = host().catalog();
		    auto result = host_render_catalog(catalog);
		const auto mission = host().loading_scenario();
		auto preload = scenario_assets(catalog, mission);
		for (const auto &definition : scenario_definitions(catalog, mission))
			if (catalog.entities.contains(definition))
				preload.push_back(definition);
		result["maps"].at(mission)["preload"] = std::move(preload);
		    auto &assets = result["asset_names"] = Json::array();
		    for (const auto &[name, bytes] : catalog.assets)
			    assets.push_back(name);
		    return result;
	    });
}

extern "C" EMSCRIPTEN_KEEPALIVE std::size_t phoenix_worker_host_asset_size(const char *name)
{
	return host().catalog().assets.at(name).size();
}

extern "C" EMSCRIPTEN_KEEPALIVE const std::uint8_t *phoenix_worker_host_asset(const char *name)
{
	return host().catalog().assets.at(name).data();
}

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_frame(double elapsed, int presentation)
{
	return respond(
	    [&]
	    {
		    auto &game = host();
		    game.poll();
		    accumulated += std::clamp(elapsed, 0.0, 0.25);
		    while (accumulated >= 1.0 / 20)
		    {
			    if (game.running())
				    game.tick();
			    accumulated -= 1.0 / 20;
		    }
		    const auto lobby = game.lobby();
		    Json seats = Json::array();
		    for (const auto &seat : lobby.consoles)
			    seats.push_back({{"slot", seat.slot}, {"id", seat.id}, {"name", seat.name}, {"member", seat.member}});
		    Json frame{{"code", lobby.code},
		               {"scenario", lobby.scenario_name},
		               {"phase", game.preparation_phase()},
		               {"status", lobby.status},
		               {"seats", std::move(seats)},
		               {"controls", game.lobby_controls()},
		               {"countdown_ms", lobby.countdown_ms},
		               {"running", game.running()},
		               {"ticks", game.ticks()}};
		    if (presentation)
		    {
			    frame["lobby"] = lobby;
			    frame["generation"] = game.generation();
			    frame["loading"] = game.loading_scenario();
			    frame["seats_available"] = game.console_seats();
			    frame["inspection"] = game.inspection();
			    frame["board"] = game.public_board();
			    if (game.running())
			    {
				    frame["scene"] = game.scene();
				    frame["viewscreen"] = game.viewscreen();
				    frame["lighting"] = game.scene_lighting();
				    frame["presentation"] = game.scene_presentation();
				    if (const auto focus = game.focus_body())
					    frame["focus"] = *focus;
				    std::set<std::uint64_t> routes{1};
				    for (const auto &seat : lobby.consoles)
					    if (seat.local_screen)
						    routes.insert(seat.local_screen);
				    for (const auto route : routes)
					    if (const auto observation = game.console_snapshot(route))
						    frame["observations"][std::to_string(route)] = network::observation_json(*observation);
			    }
		    }
		    return frame;
	    });
}

int main()
{
}
