module;
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.gameplay.craft;
import phoenix.resources;
export import phoenix.world.state;
export import phoenix.content.logistics.types;

export namespace phoenix
{
enum class CraftPhase
{
	Docked,
	Launching,
	Flying,
	Recovering,
	Lost,
	Consumed
};
struct CraftReport
{
	CraftPhase phase = CraftPhase::Docked;
	std::string order = "idle";
	EntityId target = 0;
	std::uint64_t tick = 0;
	std::int64_t energy = 0, capacity = 0, ammunition = 0, condition = 0, maximum_condition = 0;
};
struct CraftState
{
	EntityId home = 0, target = 0;
	std::string bay, order = "idle", pending_order, mission;
	EntityId pending_target = 0;
	std::size_t slot = 0;
	CraftPhase phase = CraftPhase::Docked;
	std::uint64_t revision = 1;
	std::int64_t progress = 0;
	bool ftl_recalled = false;
	CraftReport report;
	fixed::Vec rendezvous, rendezvous_velocity, target_position, target_velocity;
	std::int64_t rendezvous_altitude = 0, target_altitude = 0;
	fixed::Vec pending_position, pending_velocity;
	std::int64_t pending_altitude = 0;
};
struct CraftGuidance
{
	fixed::Vec destination;
	std::int64_t altitude = 0, speed = 0, heading = 0;
	EntityId target = 0;
	bool navigate = false;
};
struct CraftBayState
{
	std::int64_t energy_remainder = 0;
	std::vector<std::int64_t> ammunition_remainders;
};
void to_json(Json &, const CraftPhase &);
void from_json(const Json &, CraftPhase &);
void to_json(Json &, const CraftReport &);
void from_json(const Json &, CraftReport &);
void to_json(Json &, const CraftState &);
void from_json(const Json &, CraftState &);
void to_json(Json &, const CraftBayState &);
void from_json(const Json &, CraftBayState &);

struct CraftNavigationFacts
{
	Pose pose;
	std::int64_t altitude = 0, maximum_speed = 0, deceleration = 1, flight_band = 0;
	std::int64_t home_half_height = 0, own_half_height = 0, destination_half_height = 0;
	bool recovery_ready = false;
};
enum class CraftTransferProgress
{
	Unchanged,
	RecoveryAborted,
	Launched,
	Recovered
};
// Called only after the owning bay has admitted this tick's single transfer.
CraftTransferProgress advance_craft_transfer(CraftState &, const CraftBayDefinition &, bool recovery_ready);
std::int64_t stage_craft_service_energy(ResourceNetwork &source, ResourceNetwork &target, StockTransaction &stocks,
                                        ResourceIndex from, ResourceIndex to, std::int64_t offered, bool stage);
bool craft_inactive(const CraftState &);
EntityId craft_navigation_destination(const CraftState &);
CraftGuidance prepare_craft_navigation(CraftState &, const CraftDefinition &, const CraftBayDefinition &,
                                       const CraftNavigationFacts &, std::span<const BodyView>);
void advance_craft_vertical(VerticalMotion &, Pose &, Motion &, const CraftDefinition &, int effectiveness,
                            std::int64_t ticks_per_second);
} // namespace phoenix
