#include "../support/assertions.hpp"
#include "../support/fixtures.hpp"
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
import phoenix.menu;
import phoenix.navigation;
import phoenix.network.lobby;
import phoenix.network.lobby.protocol;
import phoenix.session.local;

using namespace phoenix;
using phoenix::test::rejects;
using phoenix::test::require;

int main(int argc, char **argv)
try
{
	const auto directory = "build/features-" + random_token();
	auto draft =
	    phoenix::test::fixture_pack(read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json"), "test-core");
	draft.manifest["base"] = {{"render_mode", "sprites"}, {"interface_style", "current"}};
	const auto bytes = write_pack(draft);
	MenuLibrary library(directory);
	library.launch_base(bytes);
	// A previously installed selection must not leak into a restricted launch.
	if (!PHOENIX_ENABLE_MODS)
		library.packs.push_back({"unused", "old-mod", true});
	library.launch_base(bytes);
	if (!PHOENIX_ENABLE_MODS)
		require(library.packs.size() == 1, "A base-only launch discards persisted mod selections");

	auto session = make_local_session();
	session->add_pack(bytes);
	if (!PHOENIX_ENABLE_MODS)
		rejects(
		    [&]
		    {
			    session->add_pack(bytes);
		    },
		    "Content admission refuses additional packs with mods disabled");

	session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
	session->tick();
	const auto saved = session->save();
	auto resumed = make_local_session();
	resumed->add_pack(bytes);
	resumed->restore(saved);
	require(resumed->recovery_pending(), "Saved sessions wait for explicit resume");
	resumed->resume_local();
	require(resumed->running() && resumed->ticks() == session->ticks(),
	        "Base-only sessions retain save/resume through production loading");

	Application app;
	app.can_edit = true;
	app.screen_controls = {{"menu.join-gm", "GM"},
	                       {"menu.join-ship", "New ship"},
	                       {"menu.workshop", "Workshop"},
	                       {"menu.packs", "Mods"},
	                       {"menu.join-player", "Player"}};
	const auto controls = navigation_controls(app);
	const auto contains = [&](const std::string &id)
	{
		return std::any_of(controls.begin(), controls.end(),
		                   [&](const auto &control)
		                   {
			                   return control.id == id;
		                   });
	};
	require(contains("menu.join-player"), "Same-ship player admission remains accessible");
	for (const auto &[id, enabled] :
	     std::vector<std::pair<std::string, bool>>{{"menu.join-gm", PHOENIX_ENABLE_GM},
	                                               {"menu.join-ship", PHOENIX_ENABLE_MULTISHIP},
	                                               {"menu.workshop", PHOENIX_ENABLE_WORKSHOP},
	                                               {"menu.packs", PHOENIX_ENABLE_MODS}})
	{
		require(contains(id) == enabled, "Accessible controls follow independently configured capabilities");
		if (!enabled)
			require(!invoke_navigation(app, id), "Stale accessibility actions cannot enter a disabled feature");
	}
	if (!PHOENIX_ENABLE_WORKSHOP)
		require(!invoke_navigation(app, "edit"), "Legacy authoring navigation respects the feature switch");
	NetworkOptions network;
	if (!PHOENIX_ENABLE_GM)
		rejects(
		    [&]
		    {
			    join_client_lobby(network, JoinRole::gm);
		    },
		    "Direct GM joining is disabled");
	if (!PHOENIX_ENABLE_MULTISHIP)
		rejects(
		    [&]
		    {
			    join_game_lobby(network, JoinRole::ship_server);
		    },
		    "Direct new-ship joining is disabled");
	if (argc > 1)
	{
		network.link.url = argv[1];
		network.link.room = "features-" + random_token();
		network.link.self = network.server = "feature-host";
		network.link.force_relay = true;
		network.data_location = directory + "/network";
		network.expected_base = library.base();
		auto host = make_game_lobby(network);
		host->add_pack(bytes);
		host->prepare_lobby({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		network.link.self.clear();
		auto player = join_client_lobby(network, JoinRole::player);
		const auto pump = [&](const auto &ready)
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
			while (!ready())
			{
				host->poll();
				player->poll();
				if (std::chrono::steady_clock::now() >= deadline)
					throw std::runtime_error("Crew admission timed out: " + host->lobby().status + " / " +
					                         player->lobby().status);
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		pump(
		    [&]
		    {
			    return host->lobby().members.size() == 2 && !player->lobby().self.empty();
		    });
		require(host->lobby_command({LobbyAction::approve, player->lobby().self}),
		        "Same-ship player can be approved with optional features disabled");
		pump(
		    [&]
		    {
			    for (const auto &member : player->lobby().members)
				    if (member.id == player->lobby().self)
					    return member.approved;
			    return false;
		    });
		std::cout << "Same-ship network crew admission passed\n";
	}
	std::filesystem::remove_all(directory);
	std::cout << "Feature content admission and accessible entry paths passed\n";
	return 0;
}
catch (const std::exception &error)
{
	std::cerr << error.what() << '\n';
	return 1;
}
