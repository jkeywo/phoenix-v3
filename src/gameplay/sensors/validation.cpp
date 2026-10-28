module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
module phoenix.gameplay.sensors.validation;
import phoenix.gameplay.sensors.findings;
import phoenix.knowledge.read_models;
namespace phoenix
{
namespace
{
bool valid_sensor_sample(const SensorRestoreContext &context, EntityId target, bool apparent, const SensorDefinition &d,
                         const std::string &key, const SensorSample &sample)
{
	std::int64_t required = -1;
	if (d.capabilities.contains(key) && key != "own_arcs" && key != "findings")
		required = d.capabilities.at(key);
	if (d.capabilities.contains("findings"))
		for (const auto &finding : context.findings)
			if (key == "finding:" + finding.id && finding.target == target &&
			    std::all_of(finding.capabilities.begin(), finding.capabilities.end(),
			                [&](const auto &cap)
			                {
				                return d.capabilities.contains(cap);
			                }))
				required = std::max(finding.ticks, d.capabilities.at("findings"));
	if (apparent && key == "finding:dispelled")
		required = context.ghosts.contains(target)
		               ? std::max<std::int64_t>(0, sensor_ghost_scan_duration(d, context.ghost_scan_ticks.at(target)))
		               : sample.required;
	if (apparent && key != "basic_identity" && key != "finding:dispelled")
		required = -1;
	return required >= 0 && required <= 1728000 && sample.required == required && sample.exposure >= 0 &&
	       sample.exposure <= required && (sample.data.is_null() || sample.exposure == required) &&
	       sample.observed <= context.tick && sample.data.dump().size() <= 131072;
}
} // namespace
SensorRuntime read_sensor_runtime(const Json &row, const SensorRestoreContext &context)
{
	const auto ship = context.ship;
	SensorRuntime state;
	if (row.contains("target"))
		throw std::runtime_error("Scanner intent belongs to the directed capability");
	const auto selected = context.selected;
	auto read_bearing = [&](const Json &b)
	{
		if (!b.at("id").is_number_integer() || b.at("id") <= 0 || !b.at("observed_tick").is_number_integer() ||
		    b.at("observed_tick") < 0)
			throw std::runtime_error("Invalid saved bearing");
		BearingContactView value{b.at("id").get<EntityId>(),
		                         checked_integer(b.at("bearing_mdeg"), 0, 359999, "sensor.bearing"),
		                         b.at("observed_tick").get<std::uint64_t>()};
		if (value.id == ship || value.observed_tick > context.tick)
			throw std::runtime_error("Invalid saved bearing");
		return value;
	};
	const auto bearings = row.value("bearings", Json::array());
	if (!bearings.is_array() || bearings.size() > 256)
		throw std::runtime_error("Invalid saved bearings");
	std::set<EntityId> bearing_ids;
	for (const auto &b : bearings)
	{
		const auto value = read_bearing(b);
		if (!bearing_ids.insert(value.id).second)
			throw std::runtime_error("Duplicate saved bearing");
		if (!row.contains("last_bearing") && value.id == selected)
			state.last_bearing = value;
	}
	if (row.contains("last_bearing") && !row.at("last_bearing").is_null())
	{
		const auto value = read_bearing(row.at("last_bearing"));
		if (value.id != selected)
			throw std::runtime_error("Saved bearing does not match scan target");
		state.last_bearing = value;
	}
	if (row.at("tracks").size() > 256)
		throw std::runtime_error("Too many saved sensor tracks");
	for (const auto &entry : row.at("tracks"))
	{
		const EntityId target = entry.at("target");
		auto track = entry.at("state").get<SensorTrack>();
		const auto in = [](std::int64_t v, std::int64_t bound)
		{
			return v >= -bound && v <= bound;
		};
		if (!in(track.x, fixed::unit) || !in(track.y, fixed::unit) || !in(track.vx, fixed::unit) ||
		    !in(track.vy, fixed::unit) || !in(track.altitude, 1000000) || track.heading < 0 || track.heading >= 360000)
			throw std::runtime_error("Invalid saved sensor motion");

		if (track.apparent && context.physical_entities.contains(target))
			throw std::runtime_error("Apparent track has a physical identity");
		if (context.ghosts.contains(target) && (context.ghosts.at(target).observer != ship || !track.apparent))
			throw std::runtime_error("Foreign apparent sensor track");
		if (!target || target == ship || track.samples.size() > 134 || track.seen > context.tick ||
		    state.tracks.contains(target))
			throw std::runtime_error("Invalid saved sensor track");
		for (auto &[key, sample] : track.samples)
		{
			bool valid = valid_sensor_sample(context, target, track.apparent, context.definition, key, sample);
			if (const auto *directed = context.directed)
				for (const auto &mode : directed->modes)
					if (mode.operation == DirectedOperationKind::Scan)
					{
						auto d = context.definition;
						d.capabilities = mode.capabilities;
						valid |= valid_sensor_sample(context, target, track.apparent, d, key, sample);
					}
			if (!valid)
				throw std::runtime_error("Unearned or unknown saved sensor capability");
		}
		state.tracks.emplace(target, std::move(track));
	}
	const auto &acquisitions = row.at("acquisitions");
	if (!acquisitions.is_array() || acquisitions.size() > 8192)
		throw std::runtime_error("Invalid saved directed acquisition");
	const auto *directed = context.directed;
	for (const auto &entry : acquisitions)
	{
		const std::string mode_id = entry.at("mode");
		const EntityId target = entry.at("target");
		if (!directed || !state.tracks.contains(target) || state.acquisitions[mode_id].contains(target))
			throw std::runtime_error("Invalid saved acquisition owner");
		const auto mode = std::find_if(directed->modes.begin(), directed->modes.end(),
		                               [&](const auto &m)
		                               {
			                               return m.id == mode_id && m.operation == DirectedOperationKind::Scan;
		                               });
		if (mode == directed->modes.end())
			throw std::runtime_error("Unknown saved acquisition mode");
		auto samples = entry.at("samples").get<std::map<std::string, SensorSample>>();
		if (samples.size() > 134)
			throw std::runtime_error("Too many saved acquisition samples");
		auto d = context.definition;
		d.capabilities = mode->capabilities;
		for (const auto &[key, sample] : samples)
			if (!valid_sensor_sample(context, target, state.tracks.at(target).apparent, d, key, sample))
				throw std::runtime_error("Invalid saved acquisition sample");
		state.acquisitions[mode_id].emplace(target, std::move(samples));
	}
	return state;
}
Json save_sensor_runtime(EntityId ship, const std::string &system, const SensorRuntime &state)
{
	Json tracks = Json::array();
	for (const auto &[target, t] : state.tracks)
		tracks.push_back(Json{{"target", target}, {"state", t}});
	Json acquisitions = Json::array();
	for (const auto &[mode, targets] : state.acquisitions)
		for (const auto &[target, samples] : targets)
			acquisitions.push_back({{"mode", mode}, {"target", target}, {"samples", samples}});
	return {{"ship", ship},
	        {"system", system},
	        {"tracks", tracks},
	        {"acquisitions", acquisitions},
	        {"last_bearing", state.last_bearing ? projection::bearing_json(*state.last_bearing) : Json(nullptr)}};
}
} // namespace phoenix
