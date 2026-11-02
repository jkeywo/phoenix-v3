module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module phoenix.knowledge.decision_machinery;
export import phoenix.contracts;
export import phoenix.state_tree;

export namespace phoenix
{
enum class TelemetryField : std::size_t
{
	actual_coolant_used,
	actual_cooling,
	actual_lateral,
	actual_longitudinal,
	actual_output,
	actual_steering,
	ammo,
	assisted,
	boost,
	capacity,
	charge_limit,
	coolant_depletion_ticks,
	coolant_stock,
	cooling_allocation,
	damaged,
	deceleration,
	demand_output,
	depletion_ticks,
	desired_heading,
	desired_speed,
	destination_x,
	destination_y,
	destroyed,
	discharge_limit,
	effective_boost,
	enabled,
	energy_capacity,
	flow_cut,
	follow_distance,
	follow_speed_limit,
	follow_stale,
	following,
	full_ticks,
	generator_output,
	group_allocation,
	heading_mdeg,
	heat,
	heat_capacity,
	impulse,
	impulse_charge_spent,
	impulse_charge_total,
	impulse_phase,
	impulse_progress,
	impulse_ready,
	impulse_reason,
	incoming_power,
	lateral_acceleration,
	lateral_direction,
	lateral_maximum_speed,
	lateral_thrust,
	link_isolated,
	longitudinal_thrust,
	manual_steering,
	match_speed,
	match_stale,
	matched_contact,
	matched_speed,
	maximum_rate,
	minimum_speed,
	navigating,
	officer_condition,
	overdrive,
	overheated,
	passive_cooling,
	ready,
	reload_progress,
	reload_rate_permille,
	reload_ticks,
	requested,
	reserved_power,
	selected_contact,
	shots,
	stored_energy,
	stress,
	structural_disabled,
	supply_limited,
	supply_permille,
	target,
	team_destination,
	team_location,
	team_revision,
	thermal_override,
	thermal_recovery_ticks,
	thermal_stop_ticks,
	thermal_stopped,
	travel_remaining,
	used_power,
	count
};
inline constexpr std::array<std::string_view, static_cast<std::size_t>(TelemetryField::count)> telemetry_field_names{
    "actual_coolant_used",
    "actual_cooling",
    "actual_lateral",
    "actual_longitudinal",
    "actual_output",
    "actual_steering",
    "ammo",
    "assisted",
    "boost",
    "capacity",
    "charge_limit",
    "coolant_depletion_ticks",
    "coolant_stock",
    "cooling_allocation",
    "damaged",
    "deceleration",
    "demand_output",
    "depletion_ticks",
    "desired_heading",
    "desired_speed",
    "destination_x",
    "destination_y",
    "destroyed",
    "discharge_limit",
    "effective_boost",
    "enabled",
    "energy_capacity",
    "flow_cut",
    "follow_distance",
    "follow_speed_limit",
    "follow_stale",
    "following",
    "full_ticks",
    "generator_output",
    "group_allocation",
    "heading_mdeg",
    "heat",
    "heat_capacity",
    "impulse",
    "impulse_charge_spent",
    "impulse_charge_total",
    "impulse_phase",
    "impulse_progress",
    "impulse_ready",
    "impulse_reason",
    "incoming_power",
    "lateral_acceleration",
    "lateral_direction",
    "lateral_maximum_speed",
    "lateral_thrust",
    "link_isolated",
    "longitudinal_thrust",
    "manual_steering",
    "match_speed",
    "match_stale",
    "matched_contact",
    "matched_speed",
    "maximum_rate",
    "minimum_speed",
    "navigating",
    "officer_condition",
    "overdrive",
    "overheated",
    "passive_cooling",
    "ready",
    "reload_progress",
    "reload_rate_permille",
    "reload_ticks",
    "requested",
    "reserved_power",
    "selected_contact",
    "shots",
    "stored_energy",
    "stress",
    "structural_disabled",
    "supply_limited",
    "supply_permille",
    "target",
    "team_destination",
    "team_location",
    "team_revision",
    "thermal_override",
    "thermal_recovery_ticks",
    "thermal_stop_ticks",
    "thermal_stopped",
    "travel_remaining",
    "used_power",
};
// Name resolution is an authoring/serialization-boundary operation. Runtime
// decision readers retain TelemetryField, never an authored string path.
inline std::optional<TelemetryField> telemetry_field(std::string_view name)
{
	for (std::size_t i = 0; i < telemetry_field_names.size(); ++i)
		if (telemetry_field_names[i] == name)
			return static_cast<TelemetryField>(i);
	return {};
}
class MachineryTelemetry
{
public:
	MachineryTelemetry() = default;
	MachineryTelemetry(std::initializer_list<std::pair<TelemetryField, std::int64_t>> initial)
	{
		for (const auto &[field, value] : initial)
			(*this)[field] = value;
	}
	bool contains(TelemetryField field) const
	{
		return values_.at(static_cast<std::size_t>(field)).has_value();
	}
	std::int64_t at(TelemetryField field) const
	{
		return values_.at(static_cast<std::size_t>(field)).value();
	}
	std::int64_t &operator[](TelemetryField field)
	{
		auto &value = values_.at(static_cast<std::size_t>(field));
		if (!value)
			value = 0;
		return *value;
	}
	bool operator==(const MachineryTelemetry &) const = default;

private:
	std::array<std::optional<std::int64_t>, static_cast<std::size_t>(TelemetryField::count)> values_{};
};
inline std::map<std::string, std::int64_t> encode_telemetry(const MachineryTelemetry &readings)
{
	std::map<std::string, std::int64_t> result;
	for (std::size_t i = 0; i < telemetry_field_names.size(); ++i)
		if (const auto field = static_cast<TelemetryField>(i); readings.contains(field))
			result.emplace(std::string(telemetry_field_names[i]), readings.at(field));
	return result;
}
inline MachineryTelemetry decode_telemetry(const std::map<std::string, std::int64_t> &readings)
{
	MachineryTelemetry result;
	for (const auto &[name, value] : readings)
		if (const auto field = telemetry_field(name))
			result[*field] = value;
	return result;
}
struct DecisionSystemView
{
	std::string id;
	OperatingSystem kind = OperatingSystem::Engine;
	std::int64_t requested = 0, maximum = 0;
	bool enabled = true, controlled = false;
	MachineryTelemetry telemetry;
	std::string status;
	std::int64_t effective_range = 0;
	std::vector<std::int64_t> radial_ranges;
	std::vector<SensorReachInterval> reach_profile;
	std::int64_t reach_uncertainty_mm = 0;
	bool reach_limited = false;
	std::vector<std::string> shows;
	std::vector<EntityId> contact_ids;
	bool operator==(const DecisionSystemView &) const = default;
};
struct DecisionProperty
{
	std::string id, label, kind, unit;
	std::size_t ordinal = 0;
	std::int64_t display_scale = 1;
	std::optional<std::string> choice_label;
	state_tree::Value value;
};
using DecisionProperties = std::map<std::string, DecisionProperty>;
Json encode_decision_properties(const DecisionProperties &);
struct AbilityDecisionFacts
{
	std::string phase = "unavailable";
	bool ready = false, blocked = true, requested = false, properties_available = false;
	std::int64_t preparation = 0, recovery = 0, output = 0;
	DecisionProperties properties;
};
struct ProcessDecisionFacts
{
	std::string phase = "off";
	bool requested = false, running = false, paused = false, blocked = false, finite = false, driver = false;
	std::optional<std::int64_t> activity, work;
	std::optional<std::string> target;
	std::optional<std::int64_t> progress, required_work;
	std::optional<bool> completed, ready, completion_delivered;
	DecisionProperties properties;
};
struct DecisionProcessRow
{
	std::string id, control, process, slot, label, source, owner, phase, blocker, work_unit, parent, target;
	std::uint64_t generation = 0, run = 0;
	std::int64_t output = 0, upkeep_output = 0, work = 0, display_scale = 1, progress = 0, required_work = 0;
	bool requested = false, finite = false, driver = false, details_available = false;
	bool completed = false, ready = false, completion_delivered = false;
	DecisionProperties properties;
};
inline ProcessDecisionFacts process_decision_facts(const DecisionProcessRow &row)
{
	ProcessDecisionFacts result;
	result.phase = row.phase;
	result.requested = row.requested;
	result.running = row.phase == "running";
	result.paused = row.phase == "paused";
	result.blocked = !row.blocker.empty();
	result.finite = row.finite;
	result.driver = row.driver;
	result.properties = row.properties;
	if (!row.finite || row.details_available)
	{
		result.activity = row.output;
		result.work = row.work;
	}
	if (row.details_available)
	{
		result.target = row.target;
		result.progress = row.progress;
		result.required_work = row.required_work;
		result.completed = row.completed;
		result.ready = row.ready;
		result.completion_delivered = row.completion_delivered;
	}
	return result;
}
struct MachineryDecisionSnapshot
{
	std::map<std::string, AbilityDecisionFacts> abilities;
	std::map<std::string, ProcessDecisionFacts> processes;
};
MachineryDecisionSnapshot machinery_decision_from_json(const Json &gameplay, std::uint64_t observed_tick);
} // namespace phoenix
