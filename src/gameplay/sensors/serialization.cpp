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
module phoenix.gameplay.sensors.state;
namespace phoenix
{
void to_json(Json &j, const SensorSample &value)
{
	j["exposure"] = value.exposure;
	j["required"] = value.required;
	j["observed"] = value.observed;
	j["data"] = value.data;
}
void from_json(const Json &j, SensorSample &value)
{
	const SensorSample defaults{};
	value.exposure = j.value("exposure", defaults.exposure);
	value.required = j.value("required", defaults.required);
	value.observed = j.value("observed", defaults.observed);
	value.data = j.value("data", defaults.data);
}
void to_json(Json &j, const SensorTrack &value)
{
	j["apparent"] = value.apparent;
	j["x"] = value.x;
	j["y"] = value.y;
	j["vx"] = value.vx;
	j["vy"] = value.vy;
	j["heading"] = value.heading;
	j["altitude"] = value.altitude;
	j["seen"] = value.seen;
	j["samples"] = value.samples;
}
void from_json(const Json &j, SensorTrack &value)
{
	const SensorTrack defaults{};
	value.apparent = j.value("apparent", defaults.apparent);
	value.x = j.value("x", defaults.x);
	value.y = j.value("y", defaults.y);
	value.vx = j.value("vx", defaults.vx);
	value.vy = j.value("vy", defaults.vy);
	value.heading = j.value("heading", defaults.heading);
	value.altitude = j.value("altitude", defaults.altitude);
	value.seen = j.value("seen", defaults.seen);
	value.samples = j.value("samples", defaults.samples);
}
} // namespace phoenix
