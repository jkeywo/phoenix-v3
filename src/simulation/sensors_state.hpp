#pragma once
namespace phoenix
{
struct SensorSample
{
	std::int64_t exposure = 0, required = 0;
	std::uint64_t observed = 0;
	Json data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SensorSample, exposure, required, observed, data)
struct SensorTrack
{
	bool apparent = false;
	std::int64_t x = 0, y = 0, vx = 0, vy = 0, heading = 0, altitude = 0;
	std::uint64_t seen = 0;
	std::map<std::string, SensorSample> samples;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SensorTrack, apparent, x, y, vx, vy, heading, altitude, seen, samples)
struct SensorRuntime
{
	EntityId target = 0;
	std::map<EntityId, SensorTrack> tracks;
};

} // namespace phoenix
