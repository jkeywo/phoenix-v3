module;
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay.craft;
import phoenix.gameplay.flight;

namespace phoenix
{
namespace
{
constexpr std::array<std::pair<CraftPhase, const char *>, 6> phase_names{{{CraftPhase::Docked, "docked"},
                                                                          {CraftPhase::Launching, "launching"},
                                                                          {CraftPhase::Flying, "flying"},
                                                                          {CraftPhase::Recovering, "recovering"},
                                                                          {CraftPhase::Lost, "lost"},
                                                                          {CraftPhase::Consumed, "consumed"}}};
}
void to_json(Json &json, const CraftPhase &phase)
{
	const auto found = std::find_if(phase_names.begin(), phase_names.end(),
	                                [&](const auto &entry)
	                                {
		                                return entry.first == phase;
	                                });
	json = (found == phase_names.end() ? phase_names.front() : *found).second;
}
void from_json(const Json &json, CraftPhase &phase)
{
	const auto found = std::find_if(phase_names.begin(), phase_names.end(),
	                                [&](const auto &entry)
	                                {
		                                return json == entry.second;
	                                });
	phase = (found == phase_names.end() ? phase_names.front() : *found).first;
}
void to_json(Json &json, const CraftReport &value)
{
	json["phase"] = value.phase;
	json["order"] = value.order;
	json["target"] = value.target;
	json["tick"] = value.tick;
	json["energy"] = value.energy;
	json["capacity"] = value.capacity;
	json["ammunition"] = value.ammunition;
	json["condition"] = value.condition;
	json["maximum_condition"] = value.maximum_condition;
}
void from_json(const Json &json, CraftReport &value)
{
	json.at("phase").get_to(value.phase);
	json.at("order").get_to(value.order);
	json.at("target").get_to(value.target);
	json.at("tick").get_to(value.tick);
	json.at("energy").get_to(value.energy);
	json.at("capacity").get_to(value.capacity);
	json.at("ammunition").get_to(value.ammunition);
	json.at("condition").get_to(value.condition);
	json.at("maximum_condition").get_to(value.maximum_condition);
}
void to_json(Json &json, const CraftState &value)
{
	json["home"] = value.home;
	json["target"] = value.target;
	json["bay"] = value.bay;
	json["order"] = value.order;
	json["pending_order"] = value.pending_order;
	json["pending_target"] = value.pending_target;
	json["mission"] = value.mission;
	json["slot"] = value.slot;
	json["phase"] = value.phase;
	json["revision"] = value.revision;
	json["progress"] = value.progress;
	json["ftl_recalled"] = value.ftl_recalled;
	json["report"] = value.report;
	json["rendezvous"] = value.rendezvous;
	json["rendezvous_velocity"] = value.rendezvous_velocity;
	json["target_position"] = value.target_position;
	json["target_velocity"] = value.target_velocity;
	json["rendezvous_altitude"] = value.rendezvous_altitude;
	json["target_altitude"] = value.target_altitude;
	json["pending_position"] = value.pending_position;
	json["pending_velocity"] = value.pending_velocity;
	json["pending_altitude"] = value.pending_altitude;
}
void from_json(const Json &json, CraftState &value)
{
	json.at("home").get_to(value.home);
	json.at("target").get_to(value.target);
	json.at("bay").get_to(value.bay);
	json.at("order").get_to(value.order);
	json.at("pending_order").get_to(value.pending_order);
	json.at("pending_target").get_to(value.pending_target);
	json.at("mission").get_to(value.mission);
	json.at("slot").get_to(value.slot);
	json.at("phase").get_to(value.phase);
	json.at("revision").get_to(value.revision);
	json.at("progress").get_to(value.progress);
	json.at("ftl_recalled").get_to(value.ftl_recalled);
	json.at("report").get_to(value.report);
	json.at("rendezvous").get_to(value.rendezvous);
	json.at("rendezvous_velocity").get_to(value.rendezvous_velocity);
	json.at("target_position").get_to(value.target_position);
	json.at("target_velocity").get_to(value.target_velocity);
	json.at("rendezvous_altitude").get_to(value.rendezvous_altitude);
	json.at("target_altitude").get_to(value.target_altitude);
	json.at("pending_position").get_to(value.pending_position);
	json.at("pending_velocity").get_to(value.pending_velocity);
	json.at("pending_altitude").get_to(value.pending_altitude);
}
void to_json(Json &json, const CraftBayState &value)
{
	json["energy_remainder"] = value.energy_remainder;
	json["ammunition_remainders"] = value.ammunition_remainders;
}
void from_json(const Json &json, CraftBayState &value)
{
	json.at("energy_remainder").get_to(value.energy_remainder);
	json.at("ammunition_remainders").get_to(value.ammunition_remainders);
}

bool craft_inactive(const CraftState &state)
{
	return state.phase == CraftPhase::Docked || state.phase == CraftPhase::Launching ||
	       state.phase == CraftPhase::Lost || state.phase == CraftPhase::Consumed;
}
EntityId craft_navigation_destination(const CraftState &state)
{
	if (state.order == "return")
		return state.home;
	if (state.order == "transport" || state.order == "pickup" || state.order == "hold" || state.order == "bomb" ||
	    state.order == "escort" || state.order == "defend")
		return state.target;
	return 0;
}
CraftGuidance prepare_craft_navigation(CraftState &state, const CraftDefinition &definition,
                                       const CraftBayDefinition &bay, const CraftNavigationFacts &facts,
                                       std::span<const BodyView> contacts)
{
	CraftGuidance guidance;
	guidance.destination = facts.pose.position;
	guidance.altitude = facts.altitude;
	guidance.heading = facts.pose.heading;
	guidance.target = state.target;
	const auto destination = craft_navigation_destination(state);
	if (!destination)
		return guidance;
	guidance.navigate = true;
	guidance.destination = destination == state.home ? state.rendezvous : state.target_position;
	guidance.altitude = (destination == state.home ? state.rendezvous_altitude : state.target_altitude) +
	                    (state.order == "return" ? 0 : definition.cruise_altitude + facts.flight_band);
	auto target_velocity = destination == state.home ? state.rendezvous_velocity : state.target_velocity;
	for (const auto &contact : contacts)
		if (contact.id == destination)
		{
			guidance.destination = {contact.x, contact.y};
			guidance.altitude =
			    contact.altitude + (state.order == "return" ? 0 : definition.cruise_altitude + facts.flight_band);
			target_velocity = {contact.vx, contact.vy};
		}
	if (bay.legacy_transport)
	{
		guidance.target = destination;
		if (state.order != "return")
			guidance.altitude = state.target_altitude + facts.destination_half_height + facts.own_half_height + 1000;
	}
	if (state.order == "return")
	{
		// Recover at an interior point of the real 3D envelope, above the hull.
		const auto outward = facts.pose.position - guidance.destination;
		const auto clearance = facts.home_half_height + facts.own_half_height + 1000;
		const auto vertical = std::min(clearance, bay.docking_range);
		const auto horizontal = static_cast<std::int64_t>(
		    fixed::sqrt(static_cast<std::uint64_t>(bay.docking_range * bay.docking_range - vertical * vertical)));
		const auto offset = bay.legacy_transport ? 0 : std::min(fixed::length(outward), horizontal / 2);
		guidance.destination = guidance.destination + fixed::scale(fixed::normal(outward), offset);
		const auto band_room = std::max<std::int64_t>(0, bay.docking_range / 2 - clearance);
		guidance.altitude += clearance + std::min(facts.flight_band, band_room);
	}
	const auto delta = guidance.destination - facts.pose.position;
	const auto distance = fixed::length(delta);
	const auto maximum = bay.legacy_transport ? std::min(facts.maximum_speed, bay.cruise_speed) : facts.maximum_speed;
	guidance.heading = distance > 100 ? fixed::bearing(delta) : facts.pose.heading;
	const auto separation = state.order == "bomb" ? std::min<std::int64_t>(definition.engagement_range / 2, 30000)
	                                              : (state.order == "escort" || state.order == "defend" ? 10000 : 1000);
	guidance.speed =
	    std::min(maximum, static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(
	                          2 * facts.deceleration * std::max<std::int64_t>(0, distance - separation)))));
	const auto relative_speed = std::min(guidance.speed, std::max<std::int64_t>(0, distance - separation) / 2);
	const auto desired_velocity = target_velocity + fixed::scale(fixed::normal(delta), relative_speed);
	const auto pursuit = pursuit_guidance(facts.pose, desired_velocity, maximum, guidance.heading);
	guidance.heading = pursuit.heading;
	guidance.speed = pursuit.speed;
	if (distance < bay.docking_range && state.order == "return" && state.phase == CraftPhase::Flying &&
	    facts.recovery_ready)
	{
		state.phase = CraftPhase::Recovering;
		state.progress = 0;
	}
	return guidance;
}
void advance_craft_vertical(VerticalMotion &vertical, Pose &pose, Motion &motion, const CraftDefinition &definition,
                            int effectiveness, std::int64_t ticks_per_second)
{
	pose.previous_altitude = vertical.altitude;
	const auto delta = vertical.desired - vertical.altitude;
	const auto stopping = static_cast<std::int64_t>(
	    fixed::sqrt(static_cast<std::uint64_t>(2 * definition.vertical_acceleration * std::abs(delta))));
	const auto desired = std::min(definition.vertical_speed, stopping) * (delta < 0 ? -1 : 1);
	vertical.acceleration_remainder += definition.vertical_acceleration * effectiveness / 1000;
	const auto step = vertical.acceleration_remainder / ticks_per_second;
	vertical.acceleration_remainder %= ticks_per_second;
	vertical.velocity += std::clamp(desired - vertical.velocity, -step, step);
	vertical.remainder += vertical.velocity;
	vertical.altitude = std::clamp(vertical.altitude + vertical.remainder / ticks_per_second, std::int64_t{-1000000},
	                               std::int64_t{1000000});
	vertical.remainder %= ticks_per_second;
	pose.altitude = vertical.altitude;
	motion.vertical_velocity = vertical.velocity;
}
CraftTransferProgress advance_craft_transfer(CraftState &state, const CraftBayDefinition &bay, bool recovery_ready)
{
	if (state.phase != CraftPhase::Launching && state.phase != CraftPhase::Recovering)
		return CraftTransferProgress::Unchanged;
	if (state.phase == CraftPhase::Recovering && !recovery_ready)
	{
		state.phase = CraftPhase::Flying;
		state.progress = 0;
		return CraftTransferProgress::RecoveryAborted;
	}
	if (++state.progress < bay.transfer_ticks)
		return CraftTransferProgress::Unchanged;
	const bool launch = state.phase == CraftPhase::Launching;
	state.phase = launch ? CraftPhase::Flying : CraftPhase::Docked;
	state.progress = 0;
	++state.revision;
	if (!launch)
		state.order = "idle";
	return launch ? CraftTransferProgress::Launched : CraftTransferProgress::Recovered;
}
} // namespace phoenix
