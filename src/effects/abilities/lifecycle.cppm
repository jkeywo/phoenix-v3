module;
#include <flecs.h>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

export module phoenix.effects.ability_lifecycle;
export import phoenix.effects.abilities;

export namespace phoenix
{
// Actor-scoped physical qualification, transactions and resource observations.
// Runtime phase, activation identity, progress, recovery and receipts stay owned here.
class AbilityLifecycleNative
{
public:
	virtual ~AbilityLifecycleNative();
	virtual const AbilityControlDefinition *resolve_control(const AbilityGrantState &grant) const = 0;
	virtual std::string eligibility_blocker(const AbilityControlDefinition &control, const AbilityGrantState &grant,
	                                        EntityId target) const = 0;
	virtual std::string apparatus_blocker(const AbilityGrantState &grant) const = 0;
	virtual std::string commit_activation(const AbilityGrantState &grant,
	                                      const std::optional<EffectGroupDefinition> &effect) = 0;
	virtual void request_result(const AbilityGrantState &grant, bool accepted) = 0;
	virtual std::int64_t paid_output(const std::string &grant) const = 0;
	virtual std::int64_t apparatus_effectiveness(const std::string &apparatus) const = 0;
};
void stop_ability_run(AbilityRuntime &runtime, AbilityGrantState &grant, const AbilityDefinition &definition,
                      std::uint64_t tick);

class AbilityLifecycle
{
public:
	AbilityLifecycle(flecs::entity actor, const AbilityCatalog &catalogue, std::uint64_t tick,
	                 AbilityLifecycleNative &native);
	void activate(const std::string &grant, bool activation_committed = false);
	void refresh_work(bool advance = true);

private:
	const AbilityGrantState *find(const std::string &grant) const;
	flecs::entity actor_;
	const AbilityCatalog &catalogue_;
	std::uint64_t tick_;
	AbilityLifecycleNative &native_;
};
} // namespace phoenix
