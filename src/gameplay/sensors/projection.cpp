module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
module phoenix.gameplay.sensors.projection;
import phoenix.knowledge.decision_combat;
import phoenix.knowledge.read_models;
namespace phoenix
{
SensorDecisionSnapshot capture_sensor_scanners(std::span<const SensorScannerProjection> scanners, std::uint64_t tick)
{
	SensorDecisionSnapshot out;
	for (const auto &in : scanners)
	{
		const auto &d = in.definition;
		const auto &state = *in.state;
		SensorScannerDecision v;
		v.id = in.id;
		v.target = in.selected;
		v.powered = in.powered;
		v.capabilities = d.capabilities;
		v.bearing_mdeg = in.heading;
		v.arc_mdeg = d.arc;
		v.radar_arc = d.radar_arc;
		v.range_mm = in.range;
		v.x = in.origin.x;
		v.y = in.origin.y;
		if (!in.powered)
			v.blocker = "Scanner unavailable or unpowered";
		else if (in.selected && (!state.tracks.contains(in.selected) || state.tracks.at(in.selected).seen != tick))
			v.blocker = "Target outside scanner coverage";
		if (in.mode && state.acquisitions.contains(*in.mode))
			for (const auto &[target, samples] : state.acquisitions.at(*in.mode))
				if (target == in.selected)
					for (const auto &[cap, sample] : samples)
					{
						if (d.capabilities.contains(cap) && d.capabilities.at(cap) > 0)
							v.progress.push_back({cap, sample.exposure, d.capabilities.at(cap)});
						if (cap.starts_with("finding:"))
						{
							auto at = std::find_if(v.progress.begin(), v.progress.end(),
							                       [](const auto &p)
							                       {
								                       return p.capability == "findings";
							                       });
							if (at == v.progress.end())
								v.progress.push_back({"findings", sample.exposure, sample.required});
							else
							{
								at->ticks += sample.exposure;
								at->required += sample.required;
							}
						}
					}
		for (const auto &[cap, required] : d.capabilities)
			if (required > 0 && cap != "findings" && cap != "own_arcs" &&
			    std::none_of(v.progress.begin(), v.progress.end(),
			                 [&](const auto &p)
			                 {
				                 return p.capability == cap;
			                 }))
				v.progress.push_back({cap, 0, required});
		out.scanners.push_back(std::move(v));
	}
	return out;
}
Json project_sensor_board(std::span<const SensorScannerProjection> scanners, const std::map<EntityId, Json> &evidence,
                          std::uint64_t tick)
{
	Json out{{"scanners", Json::array()}, {"contacts", Json::array()}, {"tracks", Json::array()}};
	for (const auto &scanner : capture_sensor_scanners(scanners, tick).scanners)
	{
		Json row{{"id", scanner.id},
		         {"target", scanner.target},
		         {"powered", scanner.powered},
		         {"capabilities", scanner.capabilities},
		         {"bearing_mdeg", scanner.bearing_mdeg},
		         {"arc_mdeg", scanner.arc_mdeg},
		         {"radar_arc", scanner.radar_arc},
		         {"range_mm", scanner.range_mm},
		         {"x", scanner.x},
		         {"y", scanner.y},
		         {"blocker", scanner.blocker},
		         {"progress", Json::array()}};
		for (const auto &p : scanner.progress)
			row["progress"].push_back({{"capability", p.capability}, {"ticks", p.ticks}, {"required", p.required}});
		out["scanners"].push_back(std::move(row));
	}

	for (const auto &[target, row] : evidence)
		out["contacts"].push_back(row);
	std::map<EntityId, SensorTrack> retained;
	for (const auto &input : scanners)
		for (const auto &[target, t] : input.state->tracks)
			if (!t.apparent && target <= 1000000000ULL &&
			    (!retained.contains(target) || retained.at(target).seen < t.seen))
				retained[target] = t;
	for (const auto &[target, t] : retained)
		if (t.seen < tick)
		{
			const auto age = static_cast<std::int64_t>(tick - t.seen);
			BodyView b{target,
			           "Unidentified contact",
			           std::clamp(t.x + t.vx * age / simulation_tick_hz, -fixed::unit, fixed::unit),
			           std::clamp(t.y + t.vy * age / simulation_tick_hz, -fixed::unit, fixed::unit),
			           t.vx,
			           t.vy,
			           t.heading};
			b.altitude = t.altitude;
			b.radar_icon = RadarIcon::contact;
			if (evidence.contains(target) && evidence.at(target).at("findings").contains("basic_identity"))
				b.definition = evidence.at(target).at("findings").at("basic_identity").at("data").at("class");
			out["tracks"].push_back({{"body", projection::body_json(b)},
			                         {"observed", t.seen},
			                         {"uncertainty", std::min<std::int64_t>(1000000000, age * 1000)}});
		}

	return out;
}
} // namespace phoenix
