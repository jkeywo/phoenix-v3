module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
module phoenix.gameplay.sensors.acquisition;
namespace phoenix
{
SensorAcquisitionPort::~SensorAcquisitionPort() = default;
SensorTrack *observe_sensor_contact(SensorRuntime &state, const BodyView &body, bool apparent, std::uint64_t tick)
{
	if (!state.tracks.contains(body.id) && state.tracks.size() >= 256)
		return nullptr;
	auto &track = state.tracks[body.id];
	track.apparent = apparent;
	track.x = body.x;
	track.y = body.y;
	track.vx = body.vx;
	track.vy = body.vy;
	track.heading = body.heading;
	track.altitude = body.altitude;
	track.seen = tick;
	return &track;
}
void acquire_sensor_capabilities(SensorRuntime &state, SensorTrack &track, const SensorAcquisitionInput &input,
                                 SensorAcquisitionPort &port)
{
	const auto &body = input.body;
	const auto target = body.id;
	const auto &capabilities = input.timed ? input.active.capabilities : input.passive.passive_capabilities;
	for (const auto &[cap, duration] : capabilities)
	{
		if ((input.apparent && cap != "basic_identity") || cap == "own_arcs" || cap == "findings")
			continue;
		auto &sample = input.timed ? state.acquisitions[input.mode][target][cap] : track.samples[cap];
		sample.required = duration;
		sample.exposure = std::min(duration, sample.exposure + 1);
		if (input.timed && track.samples[cap].data.is_null())
			track.samples[cap] = sample;
		if (sample.exposure < duration)
			continue;
		const auto data = input.apparent ? Json{{"class", body.definition},
		                                        {"faction", ""},
		                                        {"faction_label", body.faction_label},
		                                        {"disposition", body.disposition}}
		                                 : port.read_capability(cap);
		const bool changed = sample.data != data;
		sample.data = data;
		sample.observed = input.tick;
		if (input.timed)
			track.samples[cap] = sample;
		if (changed || input.tick % 20 == 0)
			port.publish_capability(cap, data, input.tick);
	}
}
Json sensor_track_findings(const SensorTrack &track)
{
	Json findings = Json::array();
	for (const auto &[key, sample] : track.samples)
		if (key.starts_with("finding:") && !sample.data.is_null())
		{
			auto finding = sample.data;
			finding["observed"] = sample.observed;
			findings.push_back(std::move(finding));
		}
	return findings;
}
} // namespace phoenix
