module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.network.host;
export import phoenix.session.api;
import phoenix.fleet_setup;

export namespace phoenix
{
std::shared_ptr<SessionPort> make_network_host(NetworkOptions options);
std::shared_ptr<SessionPort> make_network_host(NetworkOptions options, PreparedFleet prepared);
} // namespace phoenix
