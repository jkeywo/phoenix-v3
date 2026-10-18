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
using SystemMask = std::uint8_t;
constexpr SystemMask system_bit(OperatingSystem system)
{
	return static_cast<SystemMask>(1u << static_cast<unsigned>(system));
}
struct ConsoleAddress
{
	EntityId ship = 0;
	std::string console;
	bool operator==(const ConsoleAddress &) const = default;
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
enum class RadarIcon
{
	contact,
	ship,
	asteroid,
	sun
};
constexpr std::array<std::string_view, 4> radar_icon_names{"contact", "ship", "asteroid", "sun"};
constexpr std::optional<RadarIcon> radar_icon_named(std::string_view name)
{
	for (std::size_t i = 0; i < radar_icon_names.size(); ++i)
		if (radar_icon_names[i] == name)
			return static_cast<RadarIcon>(i);
	return {};
}
constexpr std::string_view radar_icon_name(RadarIcon icon)
{
	return radar_icon_names[static_cast<std::size_t>(icon)];
}
struct BodyView
{
	EntityId id;
	std::string definition;
	std::int64_t x, y, vx, vy, heading;
	RadarIcon radar_icon = RadarIcon::contact;
	std::string radar_sprite;
};
// Viewscreen-only visual output. Deliberately absent from radar observations.
struct SceneBody : BodyView
{
	int forward_thrust = 0; // actual applied forward acceleration, 0..1000
};
struct Observation
{
	BodyView own;
	std::vector<BodyView> contacts;
	std::int64_t requested_speed = 0, requested_heading = 0;
	bool engine_enabled = true, radar_enabled = true;
	std::string console;
	SystemMask controls = 7, feeds = 7;
};
struct OperatingCommand
{
	EntityId target;
	std::string action;
	std::int64_t value;
	// Empty in schema-1 commands; otherwise the fitted System identity on this ship.
	std::string system;
};
} // namespace phoenix
