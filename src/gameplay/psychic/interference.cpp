module;
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <tuple>
module phoenix.gameplay.psychic.interference;
namespace phoenix
{
PsychicCoveragePort::~PsychicCoveragePort() = default;
PsychicSource select_psychic_interference(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
                                          const PsychicCoveragePort &coverage)
{
	std::int64_t strength = 0;
	PsychicSource winner{};
	for (bool wards : {true, false})
		for (const auto &source : sources)
		{
			const auto &s = *source.contribution;
			if (s.work <= strength)
				continue;
			const bool ward = source.power->kind == "ward";
			if (ward != wards ||
			    (ward ? source.execution->target != receiver : source.ship == receiver || s.shapes.empty()) ||
			    !coverage.covers(source, receiver))
				continue;
			strength = s.work;
			if (!ward)
				winner = {source.ship, source.choir->id, *source.grant};
		}
	return winner;
}
} // namespace phoenix
