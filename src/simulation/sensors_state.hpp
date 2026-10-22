#pragma once
namespace phoenix
{
struct SensorSample
{
	std::int64_t exposure = 0;
	std::uint64_t observed = 0;
	Json data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SensorSample, exposure, observed, data)
struct SensorTrack
{
	std::int64_t x = 0, y = 0;
	std::uint64_t seen = 0;
	std::map<std::string, SensorSample> samples;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SensorTrack, x, y, seen, samples)
struct SensorRuntime
{
	EntityId target = 0;
	std::map<EntityId, SensorTrack> tracks;
	std::optional<BearingContactView> last_bearing;
};

} // namespace phoenix
