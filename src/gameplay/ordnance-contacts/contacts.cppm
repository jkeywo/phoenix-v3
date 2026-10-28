module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.ordnance.contacts;
export import phoenix.gameplay.ordnance;
export namespace phoenix
{
struct OrdnancePrevious
{
	fixed::Vec position;
	std::int64_t altitude = 0;
};
struct OrdnanceBodySweep
{
	EntityId id = 0;
	CollisionBody body;
	fixed::Vec start;
	std::int64_t half_height = 0;
};
struct OrdnanceMissileSweep
{
	std::uint64_t id = 0;
	EntityId source = 0;
	fixed::Vec position;
	std::int64_t altitude = 0, radius = 0;
	OrdnancePrevious previous;
};
struct OrdnanceContact
{
	std::int64_t time = 0;
	EntityId id = 0;
	std::size_t index = 0;
	bool missile = false;
};
// Owns both broad-phase indexes and deterministic swept-contact ordering for
// this tick. Native adapters filter the returned roster for current liveness
// before processing any hit; geometry never queries or mutates an ECS.
class OrdnanceContactIndex
{
	struct State;
	std::unique_ptr<State> state_;

public:
	OrdnanceContactIndex(std::vector<OrdnanceBodySweep>, std::vector<OrdnanceMissileSweep>);
	~OrdnanceContactIndex();
	OrdnanceContactIndex(const OrdnanceContactIndex &) = delete;
	OrdnanceContactIndex &operator=(const OrdnanceContactIndex &) = delete;
	std::vector<OrdnanceContact> contacts(const Projectile &, const OrdnancePrevious &) const;
};
struct PointDefenceCandidate
{
	std::uint64_t id = 0;
	EntityId target = 0;
	fixed::Vec position, velocity;
	std::int64_t altitude = 0;
};
std::uint64_t select_native_point_defence_target(EntityId observer, fixed::Vec position, fixed::Vec velocity,
                                                 std::int64_t altitude, std::int64_t range,
                                                 std::span<const PointDefenceCandidate> permitted_candidates);
} // namespace phoenix
