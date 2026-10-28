module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
module phoenix.gameplay.psychic.receptions;
namespace phoenix
{
const PsychicReceptions::Records &PsychicReceptions::records() const
{
	return records_;
}
const GhostReception &PsychicReceptions::at(EntityId id) const
{
	return records_.at(id);
}
bool PsychicReceptions::contains(EntityId id) const
{
	return records_.contains(id);
}
void PsychicReceptions::observe(const GhostProjectionKey &key, const PsychicGhostShape &shape, std::uint64_t tick,
                                EntityId &next_entity)
{
	auto found = std::find_if(records_.begin(), records_.end(),
	                          [&](const auto &entry)
	                          {
		                          const auto &g = entry.second;
		                          return g.observer == key.observer && g.source == key.source && g.choir == key.choir &&
		                                 g.grant == key.grant && g.activation == key.activation &&
		                                 g.member == key.member;
	                          });
	if (found == records_.end())
	{
		if (records_.size() >= 4096 || next_entity > 1000000000ULL)
			return;
		GhostReception g;
		g.id = next_entity++;
		g.observer = key.observer;
		g.source = key.source;
		g.choir = key.choir;
		g.grant = key.grant;
		g.member = key.member;
		g.activation = key.activation;
		g.power = key.power;
		found = records_.emplace(g.id, g).first;
	}
	auto &g = found->second;
	if (g.dispelled)
		return;
	g.last = shape;
	g.seen = tick;
	g.ended = -1;
}
void PsychicReceptions::end_unseen(std::uint64_t tick)
{
	for (auto &[id, g] : records_)
		if (g.seen != tick && g.ended < 0)
			g.ended = tick;
}
void PsychicReceptions::end_unlinked(EntityId id, std::int64_t tick)
{
	records_.at(id).ended = tick;
}
bool PsychicReceptions::dispel(EntityId id, std::uint64_t tick)
{
	auto &g = records_.at(id);
	if (g.dispelled)
		return false;
	g.dispelled = true;
	if (g.ended < 0)
		g.ended = tick;
	return true;
}
void PsychicReceptions::retire(EntityId id, std::uint64_t tick, bool source_valid, const AbilityGrantState *execution)
{
	const auto &g = records_.at(id);
	if (!source_valid || (g.ended >= 0 && tick > static_cast<std::uint64_t>(g.ended) + 200 &&
	                      (!execution || execution->activation != g.activation ||
	                       (execution->phase != "active" && execution->phase != "paused"))))
		records_.erase(id);
}
bool PsychicReceptions::restore(const GhostReception &reception)
{
	return records_.emplace(reception.id, reception).second;
}
} // namespace phoenix
