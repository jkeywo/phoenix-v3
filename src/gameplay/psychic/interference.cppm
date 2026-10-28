module;
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
export module phoenix.gameplay.psychic.interference;
export import phoenix.gameplay.psychic.contributions;
export namespace phoenix
{
using PsychicSource = std::tuple<EntityId, std::string, std::string>;
struct PsychicInterferenceSource
{
	EntityId ship;
	const std::string *grant;
	const ChoirDefinition *choir;
	const PsychicContribution *contribution;
	const AbilityGrantState *execution;
	const PsychicPowerDefinition *power;
};
class PsychicCoveragePort
{
public:
	virtual ~PsychicCoveragePort();
	virtual bool covers(const PsychicInterferenceSource &source, EntityId receiver) const = 0;
};
// Sources retain fitted apparatus/grant order; wards win equal-strength ties.
PsychicSource select_psychic_interference(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
                                          const PsychicCoveragePort &coverage);
} // namespace phoenix
