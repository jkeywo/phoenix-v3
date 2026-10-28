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
import phoenix.knowledge.read_models;
namespace phoenix
{
Json project_sensor_board(std::span<const SensorScannerProjection> scanners, const std::map<EntityId, Json> &evidence,
                          std::uint64_t tick)
{
	Json out{{"scanners", Json::array()}, {"contacts", Json::array()}, {"tracks", Json::array()}};
	for (const auto &input : scanners)
	{
		const auto &id = input.id;
		const auto &d = input.definition;
		const auto &s = *input.state;
		const auto selected = input.selected;
		Json scanner{{"id", id},
		             {"target", selected},
		             {"bearing_mdeg", input.heading},
		             {"arc_mdeg", d.arc},
		             {"range_mm", input.range},
		             {"x", input.origin.x},
		             {"y", input.origin.y},
		             {"blocker", ""},
		             {"progress", Json::array()}};
		if (!input.powered)
			scanner["blocker"] = "Scanner unavailable or unpowered";
		else if (selected && (!s.tracks.contains(selected) || s.tracks.at(selected).seen != tick))
			scanner["blocker"] = "Target outside scanner coverage";
		const auto &mode = input.mode;
		const std::map<EntityId, std::map<std::string, SensorSample>> empty_progress;
		const auto &acquisitions = mode && s.acquisitions.contains(*mode) ? s.acquisitions.at(*mode) : empty_progress;
		for (const auto &[target, samples] : acquisitions)
		{
			for (const auto &[cap, sample] : samples)
			{
				if (target == selected && d.capabilities.contains(cap) && d.capabilities.at(cap) > 0)
					scanner["progress"].push_back(
					    {{"capability", cap}, {"ticks", sample.exposure}, {"required", d.capabilities.at(cap)}});
				if (target == selected && cap.starts_with("finding:"))
				{
					auto &progress = scanner["progress"];
					auto found = std::find_if(progress.begin(), progress.end(),
					                          [](const auto &p)
					                          {
						                          return p.at("capability") == "findings";
					                          });
					if (found == progress.end())
						progress.push_back(
						    {{"capability", "findings"}, {"ticks", sample.exposure}, {"required", sample.required}});
					else
					{
						(*found)["ticks"] = found->at("ticks").get<std::int64_t>() + sample.exposure;
						(*found)["required"] = found->at("required").get<std::int64_t>() + sample.required;
					}
				}
			}
		}
		for (const auto &[cap, required] : d.capabilities)
			if (required > 0 && cap != "findings" && cap != "own_arcs" &&
			    std::none_of(scanner["progress"].begin(), scanner["progress"].end(),
			                 [&](const auto &p)
			                 {
				                 return p.at("capability") == cap;
			                 }))
				scanner["progress"].push_back({{"capability", cap}, {"ticks", 0}, {"required", required}});

		out["scanners"].push_back(scanner);
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
