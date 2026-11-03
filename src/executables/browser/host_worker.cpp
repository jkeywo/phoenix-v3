#include <emscripten.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.network.lobby;

namespace
{
using namespace phoenix;
std::shared_ptr<SessionPort> session;
std::vector<Bytes> archives;
std::size_t archive_bytes = 0;
double accumulated = 0;
std::string reply;

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
		    if (const auto url = data.value("mission_url", std::string{}); !url.empty())
			    options.pack_urls[options.expected_base.hash] = url;
		    options.link.url = data.value("service", std::string(default_network_service));
		    options.link.force_relay = true;
		    options.player_name = data.value("player", std::string("Host"));
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

extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_worker_host_frame(double elapsed)
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
		    return Json{{"code", lobby.code},
		                {"scenario", lobby.scenario_name},
		                {"phase", game.preparation_phase()},
		                {"status", lobby.status},
		                {"seats", std::move(seats)},
		                {"controls", game.lobby_controls()},
		                {"countdown_ms", lobby.countdown_ms},
		                {"running", game.running()},
		                {"ticks", game.ticks()}};
	    });
}

int main()
{
}
