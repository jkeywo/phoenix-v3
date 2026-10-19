module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

module phoenix.contracts;
namespace phoenix
{
Json encode_operating_command(const OperatingCommand &command)
{
	return {{"target", command.target},         {"action", command.action},         {"value", command.value},
	        {"system", command.system},         {"controller", command.controller}, {"channel", command.channel},
	        {"generation", command.generation}, {"request_id", command.request_id}};
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
	for (const auto *text : {&result.action, &result.system, &result.controller, &result.channel})
		if (text->size() > 256)
			throw std::runtime_error("Command identifier exceeds limit");
	return result;
}
} // namespace phoenix
