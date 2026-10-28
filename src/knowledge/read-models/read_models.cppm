module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module phoenix.knowledge.read_models;
export import phoenix.contracts;

export namespace phoenix::projection
{
// Authored radar range is at most 1e9 mm. Up to 128 officers, each with
// sixteen +10000-permille traits, can contribute to the authoritative range.
inline constexpr std::int64_t sensor_range_limit = 1000000000LL * (1 + 128 * 16 * 10);
Json body_json(const BodyView &b);
std::int64_t number(const Json &v, std::int64_t lo = -1000000000, std::int64_t hi = 1000000000);
std::string text(const Json &v, std::size_t limit = 256);
Json bearing_json(const BearingContactView &b);
BearingContactView read_bearing(const Json &v);
std::vector<BearingContactView> read_bearings(const Json &rows);
enum class BodyReadPolicy
{
	SharedDisplay,
	NetworkCompatible
};
BodyView read_body(const Json &v, BodyReadPolicy policy);
} // namespace phoenix::projection

export namespace phoenix::property_projection
{
// Effective observed values only; raw bases and authoring provenance never pass this seam.
Json observations(const Json &properties);
} // namespace phoenix::property_projection

export namespace phoenix::conversation_projection
{

Json thread(const Json &v, bool shared = false, std::size_t message_limit = 4096);
Json board(const Json &v);
} // namespace phoenix::conversation_projection

export namespace phoenix::display
{
using projection::body_json;
using projection::number;
using projection::text;
BodyView read_body(const Json &v);
Json radar_json(const SystemView &s);
SystemView read_radar(const Json &v);
// Decode and reconstruct the allowlist at both network boundaries. Never relay arbitrary nested fields.
Json sanitise(const Json &v);
} // namespace phoenix::display

export namespace phoenix::ability_projection
{
void validate_control(const Json &control);
Json board(const Json &source);
} // namespace phoenix::ability_projection

export namespace phoenix::process_projection
{
void validate_control(const Json &control);
Json board(const Json &source);
} // namespace phoenix::process_projection

export namespace phoenix::rules_projection
{
Json board(const Json &source);
}

export namespace phoenix::personnel_projection
{
Json board(const Json &source);
}

export namespace phoenix::psychic_projection
{
Json board(const Json &source);
}

export namespace phoenix::away_projection
{
Json board(const Json &rows);
}

export namespace phoenix::team_projection
{
Json board(const Json &source);
}

export namespace phoenix::craft_projection
{
Json board(const Json &rows);
}

export namespace phoenix::logistics_projection
{
Json amounts(const Json &source);
Json board(const Json &source);
} // namespace phoenix::logistics_projection

export namespace phoenix::docking_projection
{
Json board(const Json &source);
}

export namespace phoenix::sensor_projection
{
Json data(const std::string &cap, const Json &source);
Json board(const Json &source);
} // namespace phoenix::sensor_projection

export namespace phoenix::directed_projection
{
Json board(const Json &source);
}

export namespace phoenix::ftl_projection
{
Json report(const Json &v);
Json board(const Json &rows);
} // namespace phoenix::ftl_projection

export namespace phoenix::objective_projection
{
Json board(const Json &source);
}
