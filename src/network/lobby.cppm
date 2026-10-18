module;
#include <memory>
#include <string>
export module phoenix.network.lobby;
export import phoenix.session.api;
export namespace phoenix
{
std::shared_ptr<SessionPort> make_game_lobby(NetworkOptions options, bool offline = false);
std::shared_ptr<SessionPort> join_game_lobby(NetworkOptions options, JoinRole role);
} // namespace phoenix
