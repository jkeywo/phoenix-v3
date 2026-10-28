module;
#include <flecs.h>
#include <cstdint>
#include <functional>
#include <map>
export module phoenix.gameplay.psychic.emission;
export import phoenix.contracts;
export import phoenix.conversations;
export namespace phoenix
{
std::int64_t native_psychic_emission(flecs::entity entity, EntityId ship, bool unavailable, bool recent_weapon,
                                     const ConversationRuntime &conversations, std::uint64_t tick);
// The actor predicate is borrowed for one synchronous call and never retained.
class PsychicEmissionMemory
{
public:
	void record(EntityId ship, std::uint64_t tick);
	bool recent(EntityId ship, std::uint64_t tick) const;
	void expire(std::uint64_t tick, const std::function<bool(EntityId)> &actor_exists);
	Json save() const;
	void restore(const Json &rows, std::uint64_t tick, const std::function<bool(EntityId)> &actor_exists);

private:
	std::map<EntityId, std::uint64_t> emissions_;
};
} // namespace phoenix
