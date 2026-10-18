module;
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.network.lobby;
import phoenix.network.lobby.protocol;
import phoenix.network.host;
import phoenix.session.local;
import phoenix.fleet_setup;
namespace phoenix
{
namespace
{
LobbyServices server_services()
{
	LobbyServices services;
	services.world = []
	{
		return make_host_world();
	};
	services.local = []
	{
		return make_local_session();
	};
	services.host = [](NetworkOptions options)
	{
		return make_network_host(std::move(options));
	};
	services.packs = [](const SessionPort &source) -> const std::vector<Pack> &
	{
		return dynamic_cast<const HostAccess &>(source).content_packs();
	};
	services.add_pack = [](SessionPort &source, const Pack &pack)
	{
		dynamic_cast<HostAccess &>(source).add_verified_pack(pack);
	};
	services.setup = [](const SessionPort &source) -> const GameSetup &
	{
		return dynamic_cast<const HostAccess &>(source).setup();
	};
	services.resume = [](SessionPort &source)
	{
		dynamic_cast<HostAccess &>(source).resume_server();
	};
	services.prepare = [](const SessionPort &source, const GameSetup &setup, const std::string &room,
	                      const std::map<std::string, std::string> &members)
	{
		return export_fleet_setup(dynamic_cast<const HostAccess &>(source), setup, room, members);
	};
	services.recovery = [](const SessionPort &source, const Json &plan)
	{
		return save_fleet_recovery(dynamic_cast<const HostAccess &>(source), plan);
	};
	services.read = [](const Bytes &archive, const std::string &member)
	{
		auto prepared = read_fleet_setup(archive, member);
		return LobbySetup{prepared.world, prepared.plan, prepared.world->setup()};
	};
	return services;
}
} // namespace
std::shared_ptr<SessionPort> make_game_lobby(NetworkOptions options, bool offline)
{
	return compose_game_lobby(std::move(options), true, JoinRole::ship_server, offline, server_services());
}
std::shared_ptr<SessionPort> join_game_lobby(NetworkOptions options, JoinRole role)
{
	return compose_game_lobby(std::move(options), false, role, false, server_services());
}
} // namespace phoenix
