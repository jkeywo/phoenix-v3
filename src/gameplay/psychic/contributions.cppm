module;
#include <cstdint>
#include <optional>
#include <string>
export module phoenix.gameplay.psychic.contributions;
export import phoenix.gameplay.psychic.state;
export import phoenix.effects.abilities;
export import phoenix.content.sensing.types;
export namespace phoenix
{
class PsychicTargetPort
{
public:
	virtual ~PsychicTargetPort();
	virtual std::optional<Pose> projection_target_pose(EntityId target) const = 0;
};
const AbilityGrantState *find_psychic_execution(const AbilityRuntime *runtime, const PsychicContribution &contribution);
void refresh_psychic_contribution_work(PsychicChoirState &state, const AbilityRuntime *runtime,
                                       const PsychicDefinition &definition, bool source_departed);
void reconcile_psychic_contributions(PsychicChoirState &state, EntityId ship, const std::string &apparatus,
                                     const AbilityRuntime *runtime, const PsychicDefinition &definition,
                                     const PsychicTargetPort &targets);
} // namespace phoenix
