module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.network.client;
export import phoenix.session.api;

export namespace phoenix
{
std::shared_ptr<SessionPort> make_network_client(NetworkOptions options);
}
