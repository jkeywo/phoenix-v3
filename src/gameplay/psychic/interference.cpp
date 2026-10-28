module;
#include <map>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <tuple>
module phoenix.gameplay.psychic.interference;
namespace phoenix
{
PsychicInterferencePort::~PsychicInterferencePort() = default;
PsychicSource select_psychic_interference(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
                                          const PsychicInterferencePort &port)
{
	std::int64_t strength = 0;
	PsychicSource winner{};
	for (bool wards : {true, false})
		for (const auto &source : sources)
		{
			const auto &s = *source.contribution;
			if (s.work <= strength)
				continue;
			const bool ward = port.is_ward(source);
			if (ward != wards ||
			    (ward ? source.execution->target != receiver : source.ship == receiver || s.shapes.empty()) ||
			    !port.covers(source, receiver))
				continue;
			strength = s.work;
			if (!ward)
				winner = {source.ship, source.choir->id, *source.grant};
		}
	return winner;
}

void PsychicInterferenceMemory::begin_reception()
{
	winners_.clear();
}
void PsychicInterferenceMemory::observe(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
                                        const PsychicInterferencePort &port)
{
	winners_.emplace(receiver, select_psychic_interference(receiver, sources, port));
}
PsychicSource PsychicInterferenceMemory::strongest(EntityId receiver) const
{
	const auto found = winners_.find(receiver);
	return found == winners_.end() ? PsychicSource{} : found->second;
}
void PsychicInterferenceMemory::begin_concealment()
{
	concealment_.clear();
}
void PsychicInterferenceMemory::set_concealment(EntityId ship, std::int64_t strength)
{
	concealment_[ship] = strength;
}
std::int64_t PsychicInterferenceMemory::concealment(EntityId ship) const
{
	const auto found = concealment_.find(ship);
	return found == concealment_.end() ? 0 : found->second;
}
} // namespace phoenix
