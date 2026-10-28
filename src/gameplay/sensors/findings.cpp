module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
module phoenix.gameplay.sensors.findings;
namespace phoenix
{
void SensorFindings::configure(std::vector<SensorFinding> definitions)
{
	definitions_ = std::move(definitions);
	discoveries_.clear();
}
bool SensorFindings::empty() const
{
	return definitions_.empty();
}
const std::vector<SensorFinding> &SensorFindings::definitions() const
{
	return definitions_;
}
const SensorFindings::Discoveries &SensorFindings::discoveries() const
{
	return discoveries_;
}
void SensorFindings::restore_discoveries(Discoveries discoveries)
{
	discoveries_ = std::move(discoveries);
}
SensorFindingUpdate SensorFindings::advance(SensorRuntime &state, SensorTrack &track, const std::string &mode,
                                            EntityId ship, EntityId target, const SensorFinding &f,
                                            std::int64_t finding_ticks, std::uint64_t tick)
{
	auto &sample = state.acquisitions[mode][target]["finding:" + f.id];
	const auto duration = std::max(f.ticks, finding_ticks);
	sample.required = duration;
	sample.exposure = std::min(duration, sample.exposure + 1);
	if (track.samples["finding:" + f.id].data.is_null())
		track.samples["finding:" + f.id] = sample;
	if (sample.exposure < duration)
		return {};
	const bool changed = sample.data.is_null();
	sample.data = Json{{"id", f.id}, {"text", f.text}};
	if (!f.tag.empty())
		sample.data["tag"] = f.tag;
	sample.observed = tick;
	track.samples["finding:" + f.id] = sample;
	const bool discovered = discoveries_.emplace(ship, target, f.id).second;
	return {true, changed, discovered};
}
Json SensorFindings::forwarded_payload(EntityId ship, const SensorFinding &finding, std::uint64_t tick) const
{
	Json delivered = Json::array();
	for (const auto &known : definitions_)
		if (known.target == finding.target && known.destination_station == finding.destination_station &&
		    !known.tag.empty() && discoveries_.contains({ship, known.target, known.id}))
			delivered.push_back({{"id", known.id}, {"tag", known.tag}, {"text", known.text}, {"observed", tick}});
	return delivered;
}
std::int64_t sensor_ghost_scan_duration(const SensorDefinition &d, std::int64_t profile_ticks)
{
	if (!d.capabilities.contains("compartment_condition") && !d.capabilities.contains("findings"))
		return -1;
	return std::max(
	    profile_ticks,
	    d.capabilities.at(d.capabilities.contains("compartment_condition") ? "compartment_condition" : "findings"));
}
bool advance_ghost_acquisition(SensorRuntime &state, SensorTrack &track, const std::string &mode, EntityId target,
                               std::int64_t duration)
{
	if (duration < 0)
		return false;
	auto &sample = state.acquisitions[mode][target]["finding:dispelled"];
	sample.required = duration;
	sample.exposure = std::min(duration, sample.exposure + 1);
	if (track.samples["finding:dispelled"].data.is_null())
		track.samples["finding:dispelled"] = sample;
	return sample.exposure >= duration;
}
void record_sensor_dispel_sample(SensorTrack &track, std::int64_t duration, std::uint64_t tick)
{
	auto &sample = track.samples["finding:dispelled"];
	sample.required = std::max<std::int64_t>(0, duration);
	sample.exposure = sample.required;
	sample.observed = tick;
	sample.data = {{"id", "dispelled"}, {"text", "Psychic projection dispelled"}};
}
} // namespace phoenix
