module;
#include <cstdint>
#include <nlohmann/json.hpp>

export module phoenix.networking.projection;
export import phoenix.contracts;
export import phoenix.content.catalog;

export namespace phoenix::network
{
Json bounded_gameplay(const Json &source);
Json public_board_json(const Json &source);
Json observation_json(const Observation &observation);
Observation read_observation(const Json &value, const Catalog &catalog);
std::uint64_t counter(const Json &value);
} // namespace phoenix::network
