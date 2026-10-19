module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <stdexcept>
#include <limits>

export module phoenix.contracts;
export import phoenix.data;

export namespace phoenix
{
using EntityId = std::uint64_t;
enum class OperatingAction
{
	Speed,
	Heading,
	EngineEnabled,
	RadarEnabled,
	Assisted,
	Longitudinal,
	Lateral,
	ManualSteering,
	Boost,
	Impulse,
	MatchSpeed,
	SelectContact,
	GeneratorOutput,
	ChargeLimit,
	DischargeLimit,
	GroupAllocation,
	LinkIsolation,
	CoolingAllocation,
	ThermalOverride,
	Overdrive,
	SystemEnabled,
	Destination,
	FollowContact
};
enum class OperatingSystem
{
	Engine,
	Steering,
	Radar,
	Generator,
	Storage,
	Distribution,
	Coolant,
	Lateral,
	Boost,
	Impulse
};
using SystemMask = std::uint32_t;
constexpr SystemMask system_bit(OperatingSystem system)
{
	return static_cast<SystemMask>(1u << static_cast<unsigned>(system));
}
struct ConsoleAddress
{
	EntityId ship = 0;
	std::string console;
	bool operator==(const ConsoleAddress &) const = default;
	bool operator<(const ConsoleAddress &v) const
	{
		return ship < v.ship || (ship == v.ship && console < v.console);
	}
};
// Stable logical identity. Never serialize a Flecs handle as Controller authority.
struct ControllerAddress
{
	EntityId entity = 0;
	std::string console, controller;
	bool operator==(const ControllerAddress &) const = default;
	bool operator<(const ControllerAddress &v) const
	{
		if (entity != v.entity)
			return entity < v.entity;
		return console < v.console || (console == v.console && controller < v.controller);
	}
};
enum class CommandSemantics : std::uint8_t
{
	Setting,
	Request
};
struct OperatingActionSpec
{
	OperatingAction action;
	std::string_view name;
	OperatingSystem system;
	bool boolean;
	std::int64_t maximum;
	std::int64_t minimum = 0;
};
inline constexpr std::array operating_actions{
    OperatingActionSpec{OperatingAction::Speed, "speed_mm_s", OperatingSystem::Engine, false, 1000000000, -1000000000},
    OperatingActionSpec{OperatingAction::Heading, "heading_mdeg", OperatingSystem::Steering, false, 359999},
    OperatingActionSpec{OperatingAction::EngineEnabled, "engine_enabled", OperatingSystem::Engine, true, 1},
    OperatingActionSpec{OperatingAction::RadarEnabled, "radar_enabled", OperatingSystem::Radar, true, 1},
    OperatingActionSpec{OperatingAction::Assisted, "assisted", OperatingSystem::Engine, true, 1},
    OperatingActionSpec{OperatingAction::Longitudinal, "longitudinal_thrust", OperatingSystem::Engine, false, 1000,
                        -1000},
    OperatingActionSpec{OperatingAction::Lateral, "lateral_thrust", OperatingSystem::Lateral, false, 1000, -1000},
    OperatingActionSpec{OperatingAction::ManualSteering, "manual_steering", OperatingSystem::Steering, false, 1000,
                        -1000},
    OperatingActionSpec{OperatingAction::Boost, "boost", OperatingSystem::Boost, true, 1},
    OperatingActionSpec{OperatingAction::Impulse, "impulse", OperatingSystem::Impulse, true, 1},
    OperatingActionSpec{OperatingAction::MatchSpeed, "match_speed", OperatingSystem::Engine, true, 1},
    OperatingActionSpec{OperatingAction::SelectContact, "selected_contact", OperatingSystem::Engine, false, 1000000000},
    OperatingActionSpec{OperatingAction::GeneratorOutput, "generator_output", OperatingSystem::Generator, false,
                        1000000000000},
    OperatingActionSpec{OperatingAction::ChargeLimit, "charge_limit", OperatingSystem::Storage, false, 1000000000000},
    OperatingActionSpec{OperatingAction::DischargeLimit, "discharge_limit", OperatingSystem::Storage, false,
                        1000000000000},
    OperatingActionSpec{OperatingAction::GroupAllocation, "group_allocation", OperatingSystem::Distribution, false,
                        1000000000000},
    OperatingActionSpec{OperatingAction::LinkIsolation, "link_isolated", OperatingSystem::Distribution, true, 1},
    OperatingActionSpec{OperatingAction::CoolingAllocation, "cooling_allocation", OperatingSystem::Coolant, false,
                        1000000000000},
    OperatingActionSpec{OperatingAction::ThermalOverride, "thermal_override", OperatingSystem::Storage, true, 1},
    OperatingActionSpec{OperatingAction::Overdrive, "overdrive", OperatingSystem::Storage, true, 1},
    OperatingActionSpec{OperatingAction::SystemEnabled, "system_enabled", OperatingSystem::Storage, true, 1},
    OperatingActionSpec{OperatingAction::Destination, "destination", OperatingSystem::Engine, true, 1},
    OperatingActionSpec{OperatingAction::FollowContact, "follow_contact", OperatingSystem::Engine, true, 1}};
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
struct OperatingArgumentSpec
{
	std::string_view name;
	std::int64_t minimum, maximum;
};
inline constexpr std::array destination_arguments{OperatingArgumentSpec{"x", -1000000000, 1000000000},
                                                  OperatingArgumentSpec{"y", -1000000000, 1000000000}};
inline constexpr std::array follow_arguments{OperatingArgumentSpec{"contact", 0, 1000000000},
                                             OperatingArgumentSpec{"distance", 1, 1000000000},
                                             OperatingArgumentSpec{"speed", 1, 1000000000}};
constexpr std::span<const OperatingArgumentSpec> operating_arguments(OperatingAction action)
{
	if (action == OperatingAction::Destination)
		return destination_arguments;
	if (action == OperatingAction::FollowContact)
		return follow_arguments;
	return {};
}
inline bool operating_arguments_valid(OperatingAction action, std::int64_t value,
                                      std::span<const std::int64_t> arguments)
{
	const auto fields = operating_arguments(action);
	if (arguments.size() != fields.size())
		return false;
	for (std::size_t i = 0; i < fields.size(); ++i)
		if (arguments[i] < fields[i].minimum || arguments[i] > fields[i].maximum)
			return false;
	return action != OperatingAction::FollowContact || !value || arguments[0] > 0;
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
	case OperatingSystem::Generator:
		return "generator";
	case OperatingSystem::Storage:
		return "storage";
	case OperatingSystem::Distribution:
		return "distribution";
	case OperatingSystem::Coolant:
		return "coolant";
	case OperatingSystem::Lateral:
		return "lateral";
	case OperatingSystem::Boost:
		return "boost";
	case OperatingSystem::Impulse:
		return "impulse";
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
	return value >= spec.minimum && value <= spec.maximum;
}
constexpr bool operating_permitted(OperatingAction action, std::int64_t value,
                                   const OperatingCapabilities &capabilities)
{
	const auto &spec = operating_action(action);
	return action <= OperatingAction::RadarEnabled && capabilities.contains(spec.system) &&
	       operating_value_valid(spec, value) &&
	       (action != OperatingAction::Speed || (value >= 0 && value <= *capabilities.engine_max_speed));
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
struct SystemView
{
	std::string id;
	OperatingSystem kind = OperatingSystem::Engine;
	std::int64_t requested = 0, maximum = 0;
	bool enabled = true;
	bool controlled = false;
	std::map<std::string, std::int64_t> telemetry;
	std::string status;
	bool operator==(const SystemView &) const = default;
};
struct Observation
{
	BodyView own;
	std::vector<BodyView> contacts;
	std::int64_t requested_speed = 0, requested_heading = 0;
	bool engine_enabled = true, radar_enabled = true;
	std::string console;
	SystemMask controls = 7, feeds = 7;
	std::vector<SystemView> systems;
	// Permitted knowledge and coordination serialization boundary; never AI memory.
	Json gameplay = Json::object();
	std::uint64_t observed_tick = 0;
};
struct OperatingCommand
{
	EntityId target;
	std::string action;
	std::int64_t value;
	// Empty in schema-1 commands; otherwise the fitted System identity on this ship.
	std::string system;
	std::string controller, channel;
	std::uint64_t generation = 0, request_id = 0;
	std::vector<std::int64_t> arguments;
};
Json encode_operating_command(const OperatingCommand &command);
OperatingCommand decode_operating_command(const Json &value);
// Capability masks above are legacy presentation hints. Instance identities are
// the command authority boundary, including when several instances share a kind.
inline std::optional<std::string> select_system(const std::vector<SystemView> &systems, OperatingSystem kind,
                                                std::string_view requested = {}, bool control = true)
{
	std::optional<std::string> result;
	for (const auto &system : systems)
		if (system.kind == kind && (!control || system.controlled) && (requested.empty() || system.id == requested))
		{
			if (result)
				return {};
			result = system.id;
		}
	return result;
}
} // namespace phoenix
