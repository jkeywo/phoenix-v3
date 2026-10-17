module;
#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module phoenix.contracts;

export namespace phoenix
{
using EntityId = std::uint64_t;
enum class OperatingAction
{
	Speed,
	Heading,
	EngineEnabled,
	RadarEnabled
};
enum class OperatingSystem
{
	Engine,
	Steering,
	Radar
};
struct OperatingActionSpec
{
	OperatingAction action;
	std::string_view name;
	OperatingSystem system;
	bool boolean;
	std::int64_t maximum;
};
inline constexpr std::array operating_actions{
    OperatingActionSpec{OperatingAction::Speed, "speed_mm_s", OperatingSystem::Engine, false, 1000000000},
    OperatingActionSpec{OperatingAction::Heading, "heading_mdeg", OperatingSystem::Steering, false, 359999},
    OperatingActionSpec{OperatingAction::EngineEnabled, "engine_enabled", OperatingSystem::Engine, true, 1},
    OperatingActionSpec{OperatingAction::RadarEnabled, "radar_enabled", OperatingSystem::Radar, true, 1}};
constexpr const OperatingActionSpec *operating_action(std::string_view name)
{
	for (const auto &spec : operating_actions)
		if (spec.name == name)
			return &spec;
	return nullptr;
}
constexpr const OperatingActionSpec &operating_action(OperatingAction action)
{
	return operating_actions[static_cast<std::size_t>(action)];
}
constexpr std::string_view operating_system_name(OperatingSystem system)
{
	switch (system)
	{
	case OperatingSystem::Engine:
		return "engine";
	case OperatingSystem::Steering:
		return "steering";
	case OperatingSystem::Radar:
		return "radar";
	}
	return {};
}
struct OperatingCapabilities
{
	std::optional<std::int64_t> engine_max_speed;
	bool steering = false, radar = false;
	constexpr bool contains(OperatingSystem system) const
	{
		switch (system)
		{
		case OperatingSystem::Engine:
			return engine_max_speed.has_value();
		case OperatingSystem::Steering:
			return steering;
		case OperatingSystem::Radar:
			return radar;
		}
		return false;
	}
};
constexpr bool operating_value_valid(const OperatingActionSpec &spec, std::int64_t value)
{
	return value >= 0 && value <= spec.maximum;
}
constexpr bool operating_permitted(OperatingAction action, std::int64_t value,
                                   const OperatingCapabilities &capabilities)
{
	const auto &spec = operating_action(action);
	return capabilities.contains(spec.system) && operating_value_valid(spec, value) &&
	       (action != OperatingAction::Speed || value <= *capabilities.engine_max_speed);
}
struct BodyView
{
	EntityId id;
	std::string definition;
	std::int64_t x, y, vx, vy, heading;
};
struct Observation
{
	BodyView own;
	std::vector<BodyView> contacts;
	std::int64_t requested_speed = 0, requested_heading = 0;
	bool engine_enabled = true, radar_enabled = true;
};
struct OperatingCommand
{
	EntityId target;
	std::string action;
	std::int64_t value;
};
} // namespace phoenix
