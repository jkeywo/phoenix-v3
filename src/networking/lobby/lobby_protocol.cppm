module;
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.network.lobby.protocol;
export import phoenix.session.api;
export namespace phoenix
{
// Server composition is injected; crew/GM lobby participants never link simulation.
struct LobbySetup
{
	std::shared_ptr<SessionPort> world;
	Json plan;
	GameSetup setup;
};
struct LobbyPreparation
{
	Bytes bundle;
	std::shared_ptr<SessionPort> world;
	std::function<std::shared_ptr<SessionPort>(NetworkOptions)> host;
};
struct LobbyServices
{
	std::function<std::shared_ptr<SessionPort>()> world, local;
	std::function<std::shared_ptr<SessionPort>(NetworkOptions)> host;
	std::function<const std::vector<Pack> &(const SessionPort &)> packs;
	std::function<std::shared_ptr<const VerifiedPacks>(const SessionPort &)> verified;
	std::function<void(SessionPort &, const Pack &)> add_pack;
	std::function<void(SessionPort &, std::shared_ptr<const VerifiedPacks>)> install_verified;
	std::function<const GameSetup &(const SessionPort &)> setup;
	std::function<void(SessionPort &)> resume;
	std::function<LobbyPreparation(std::shared_ptr<const VerifiedPacks>, const GameSetup &, const std::string &,
	                               const std::map<std::string, std::string> &)>
	    prepare;
	std::function<Bytes(const SessionPort &, const Json &)> recovery;
	std::function<LobbySetup(const Bytes &, const std::string &)> read;
};
std::shared_ptr<SessionPort> compose_game_lobby(NetworkOptions, bool coordinator, JoinRole, bool offline,
                                                LobbyServices);
std::shared_ptr<SessionPort> join_client_lobby(NetworkOptions, JoinRole);
} // namespace phoenix
