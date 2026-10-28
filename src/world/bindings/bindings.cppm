module;
#include "native_headers.hpp"

export module phoenix.simulation:bindings;
export import :runtime;

// Actor-scoped ability physical binding, shared by the ordinary lifecycle units.
export namespace phoenix::world_detail
{
struct AbilityLifecycleBridge final : AbilityLifecycleNative
{
	Runtime &world;
	EntityId ship;
	AbilityLifecycleBridge(Runtime &owner, EntityId actor);
	const AbilityControlDefinition *resolve_control(const AbilityGrantState &grant) const override;
	std::string eligibility_blocker(const AbilityControlDefinition &control, const AbilityGrantState &grant,
	                                EntityId target) const override;
	std::string apparatus_blocker(const AbilityGrantState &grant) const override;
	std::string commit_activation(const AbilityGrantState &grant,
	                              const std::optional<EffectGroupDefinition> &effect) override;
	void request_result(const AbilityGrantState &grant, bool accepted) override;
	std::int64_t paid_output(const std::string &grant) const override;
	std::int64_t apparatus_effectiveness(const std::string &apparatus) const override;
};
} // namespace phoenix::world_detail
