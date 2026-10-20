module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

module phoenix.contracts;
namespace phoenix
{
Json sensor_reach_json(const SystemView &sensor)
{
	Json out = Json::array();
	for (const auto &v : sensor.reach_profile)
		out.push_back(Json::array({v.end_mdeg, v.range_mm}));
	return out;
}
void read_sensor_reach(SystemView &sensor, const Json &profile)
{
	if (!profile.is_array() || profile.size() > 1024 || (sensor.kind != OperatingSystem::Radar && !profile.empty()))
		throw std::runtime_error("Invalid projected sensor profile");
	sensor.reach_profile.clear();
	std::int64_t previous = 0;
	for (const auto &interval : profile)
	{
		if (!interval.is_array() || interval.size() != 2 || !interval[0].is_number_integer() ||
		    !interval[1].is_number_integer())
			throw std::runtime_error("Invalid sensor interval");
		SensorReachInterval v{interval[0], interval[1]};
		if (v.end_mdeg <= previous || v.end_mdeg > 360000 || v.range_mm < 0 || v.range_mm > sensor.effective_range)
			throw std::runtime_error("Invalid sensor interval bounds");
		previous = v.end_mdeg;
		sensor.reach_profile.push_back(v);
	}
	if (!profile.empty() && previous != 360000)
		throw std::runtime_error("Incomplete sensor profile");
}
ShipCameraState ship_camera_state(const Json &board)
{
	ShipCameraState result;
	bool found = false;
	for (const auto &record : board.value("shared", Json::array()))
		if (record.is_object() && record.value("schema", "") == "phoenix.camera")
		{
			if (found || record.value("withdrawn", false))
				throw std::runtime_error("Invalid shared camera record");
			found = true;
			const auto &fields = record.at("fields");
			const auto mode = checked_integer(fields.at("mode"), 0, 1, "camera.mode");
			const auto direction = checked_integer(fields.at("direction"), 0, 7, "camera.direction");
			result = {static_cast<int>(mode), static_cast<int>(direction)};
		}
	return result;
}
Json encode_operating_command(const OperatingCommand &command)
{
	return {{"target", command.target},
	        {"action", command.action},
	        {"value", command.value},
	        {"system", command.system},
	        {"controller", command.controller},
	        {"channel", command.channel},
	        {"generation", command.generation},
	        {"request_id", command.request_id},
	        {"arguments", command.arguments},
	        {"console", command.console},
	        {"speaker_name", command.speaker_name},
	        {"speaker_avatar", command.speaker_avatar}};
}
OperatingCommand decode_operating_command(const Json &value)
{
	const auto counter = [&](const char *key, std::uint64_t fallback)
	{
		if (!value.contains(key))
			return fallback;
		const auto &n = value.at(key);
		if (!n.is_number_integer() || n < 0 || n > std::numeric_limits<std::uint64_t>::max())
			throw std::runtime_error("Invalid command counter");
		return n.get<std::uint64_t>();
	};
	if (!value.at("value").is_number_integer() || value.at("value") < std::numeric_limits<std::int64_t>::min() ||
	    value.at("value") > std::numeric_limits<std::int64_t>::max())
		throw std::runtime_error("Invalid command value");
	OperatingCommand result{counter("target", 0),
	                        value.at("action").get<std::string>(),
	                        value.at("value").get<std::int64_t>(),
	                        value.value("system", std::string{}),
	                        value.value("controller", std::string{}),
	                        value.value("channel", std::string{}),
	                        counter("generation", 0),
	                        counter("request_id", 0)};
	if (value.contains("arguments"))
	{
		const auto &arguments = value.at("arguments");
		if (!arguments.is_array() || arguments.size() > 3)
			throw std::runtime_error("Invalid command arguments");
		for (const auto &argument : arguments)
		{
			if (!argument.is_number_integer() || argument < -1000000000 || argument > 1000000000)
				throw std::runtime_error("Invalid command argument");
			result.arguments.push_back(argument.get<std::int64_t>());
		}
	}
	result.console = value.value("console", std::string{});
	result.speaker_name = value.value("speaker_name", std::string{});
	const auto avatar = value.value("speaker_avatar", Json::array());
	if (result.speaker_name.size() > 64 ||
	    std::any_of(result.speaker_name.begin(), result.speaker_name.end(),
	                [](unsigned char c)
	                {
		                return c < 32 || c == 127;
	                }) ||
	    !avatar.is_array() || avatar.size() > 12288)
		throw std::runtime_error("Invalid message speaker profile");
	for (const auto &byte : avatar)
	{
		if (!byte.is_number_integer() || byte < 0 || byte > 255)
			throw std::runtime_error("Invalid speaker avatar byte");
		result.speaker_avatar.push_back(byte.get<std::uint8_t>());
	}
	for (const auto *text : {&result.action, &result.system, &result.controller, &result.channel, &result.console})
		if (text->size() > 256)
			throw std::runtime_error("Command identifier exceeds limit");
	return result;
}
} // namespace phoenix
