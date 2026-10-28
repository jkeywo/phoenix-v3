module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
export module phoenix.gameplay.psychic.receptions;
export import phoenix.gameplay.psychic.state;
export import phoenix.effects.abilities;
export namespace phoenix
{
struct GhostProjectionKey
{
	EntityId observer = 0, source = 0;
	std::string choir, grant, power;
	std::uint64_t activation = 0;
	std::size_t member = 0;
};
// Reception identity and rejection receipts survive native apparatus pauses.
class PsychicReceptions
{
public:
	using Records = std::map<EntityId, GhostReception>;
	const Records &records() const;
	const GhostReception &at(EntityId id) const;
	bool contains(EntityId id) const;
	void observe(const GhostProjectionKey &key, const PsychicGhostShape &shape, std::uint64_t tick,
	             EntityId &next_entity);
	void end_unseen(std::uint64_t tick);
	void end_unlinked(EntityId id, std::int64_t tick);
	bool dispel(EntityId id, std::uint64_t tick);
	void retire(EntityId id, std::uint64_t tick, bool source_valid, const AbilityGrantState *execution);
	bool restore(const GhostReception &reception);

private:
	Records records_;
};
} // namespace phoenix
