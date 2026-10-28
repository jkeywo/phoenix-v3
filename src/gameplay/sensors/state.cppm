module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>
export module phoenix.gameplay.sensors.state;
export import phoenix.world.state;

export namespace phoenix
{

struct SensorSample
{
	std::int64_t exposure = 0, required = 0;
	std::uint64_t observed = 0;
	Json data;
};
void to_json(Json &, const SensorSample &);
void from_json(const Json &, SensorSample &);
struct SensorTrack
{
	bool apparent = false;
	std::int64_t x = 0, y = 0, vx = 0, vy = 0, heading = 0, altitude = 0;
	std::uint64_t seen = 0;
	std::map<std::string, SensorSample> samples;
};
void to_json(Json &, const SensorTrack &);
void from_json(const Json &, SensorTrack &);
struct SensorRuntime
{
	std::map<EntityId, SensorTrack> tracks;
	std::optional<BearingContactView> last_bearing;
	// Acquisition belongs to its authored mode; published evidence stays on tracks.
	std::map<std::string, std::map<EntityId, std::map<std::string, SensorSample>>> acquisitions;
};

} // namespace phoenix
