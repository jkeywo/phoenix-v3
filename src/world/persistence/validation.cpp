module;
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.world.persistence;

namespace phoenix
{
void validate_checkpoint_envelope(const Json &saved, std::span<const std::pair<std::string, std::string>> packs)
{
	if (!saved.at("tick").is_number_integer() || saved.at("tick") < 0 ||
	    saved.at("tick") > std::numeric_limits<std::int64_t>::max())
		throw std::runtime_error("Saved state invalid tick");
	if (saved.at("schema") != 33)
		throw std::runtime_error("Saved state schema retired; start a new session (schema 33 required)");
	if (saved.at("packs") != Json(std::vector(packs.begin(), packs.end())))
		throw std::runtime_error("Saved state content identity mismatch; start a new session");
	if (saved.contains("delegated") || saved.contains("preset"))
		throw std::runtime_error("Legacy authority fields are not Controller continuation");
	if (!saved.value("carried_craft", Json::array()).empty())
		throw std::runtime_error("Legacy physical-craft save requires a new game");
	for (const auto &mission : saved.at("away_missions"))
		if (mission.value("craft", EntityId{0}) || mission.value("returning_without_delivery", false))
			throw std::runtime_error("Legacy physical-craft commitment requires a new game");
}
EntityId checkpoint_next_entity(const Json &saved, EntityId largest_existing)
{
	const auto &value = saved.at("next_entity");
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 1))
		throw std::runtime_error("Invalid saved entity allocator");
	const auto next = value.get<EntityId>();
	if (next < 1 || next > 1000000001ULL || next <= largest_existing)
		throw std::runtime_error("Saved entity allocator overlaps roster");
	return next;
}
CheckpointRoster::CheckpointRoster(const Json &bodies, std::span<const EntityId> expected)
    : expected_(expected.begin(), expected.end())
{
	if (!bodies.is_array() || bodies.size() != expected.size())
		throw std::runtime_error("Saved state entity roster mismatch");
}
void CheckpointRoster::accept(EntityId id)
{
	if (!seen_.insert(id).second || !expected_.contains(id))
		throw std::runtime_error("Saved state duplicate/unknown entity");
}
void validate_checkpoint_entity_authority(const Json &value)
{
	if (value.contains("operation") || value.contains("controllers"))
		throw std::runtime_error("Legacy entity authority is not Controller continuation");
}
void validate_checkpoint_body(const SavedBody &restored, const std::string &definition, bool movable)
{
	if (restored.definition != definition)
		throw std::runtime_error("Saved state definition mismatch");
	if (!movable && (restored.velocity.x || restored.velocity.y))
		throw std::runtime_error("Saved static entity cannot have velocity");
	for (auto v : {restored.position.x, restored.position.y, restored.velocity.x, restored.velocity.y})
		if (v < -fixed::unit || v > fixed::unit)
			throw std::runtime_error("Saved state position/velocity outside supported bounds");
	if (restored.heading < 0 || restored.heading >= 360000 || restored.remainder.x <= -simulation_tick_hz ||
	    restored.remainder.x >= simulation_tick_hz || restored.remainder.y <= -simulation_tick_hz ||
	    restored.remainder.y >= simulation_tick_hz)
		throw std::runtime_error("Saved state invalid numeric remainder/heading");
}
void validate_checkpoint_altitude(std::int64_t altitude)
{
	if (std::abs(altitude) > 1000000)
		throw std::runtime_error("Invalid saved body altitude");
}
void validate_checkpoint_vertical(const VerticalMotion &v)
{
	if (std::abs(v.altitude) > 1000000 || std::abs(v.desired) > 1000000 || std::abs(v.velocity) > 1000000 ||
	    std::abs(v.remainder) >= simulation_tick_hz || std::abs(v.acceleration_remainder) >= simulation_tick_hz)
		throw std::runtime_error("Invalid saved vertical flight");
}
} // namespace phoenix
