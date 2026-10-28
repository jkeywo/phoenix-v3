module;
#include <flecs.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.effects.properties.runtime;
export import phoenix.effects.properties;
import phoenix.world.state;

export namespace phoenix
{
// Notifications bracket authoritative modifier mutations. Native integration
// captures custom-property changes and publishes their cross-domain events.
struct PropertyMutationObserver
{
	std::function<void(flecs::entity)> before_owner_change;
	std::function<void(const EffectTarget &)> before_removal;
	std::function<void()> after_change;
};
using ModifierLogicalTargetAvailable = std::function<bool(flecs::entity, const std::string &)>;
using ModifierTargetCompatible =
    std::function<bool(flecs::entity, const std::string &, std::span<const PropertyValue>)>;
using ModifierOwnerLookup = std::function<flecs::entity(EntityId)>;

PropertyTargetKey modifier_target_key(flecs::entity, const std::string &);
PropertyTargetKey modifier_target_key(const EffectTarget &);
EffectTarget resolve_property_target(flecs::entity, const std::string &);
const PropertyEffectOwner *rebuild_property_aggregates(flecs::entity);
PropertyAggregateValue read_modifier_aggregate(flecs::entity, const PropertyTargetKey &, PropertyKey,
                                               const std::string &custom, std::map<std::string, bool> &known_grants,
                                               const std::function<bool(const std::string &)> &grant_available);

// Owns actual ECS modifier membership, queries, cache invalidation and snapshot
// reconciliation. Logical target authority and property-change events remain native.
class PropertyApplicationStore
{
public:
	void initialise(flecs::world &);
	void erase(flecs::entity, const PropertyMutationObserver &);
	void commit(flecs::world &, flecs::entity, const PropertyEffects &, const PropertyMutationObserver &);
	void expire(std::int64_t tick, const PropertyMutationObserver &);
	void prune(const ModifierLogicalTargetAvailable &, const PropertyMutationObserver &);
	Json save() const;
	void restore(flecs::world &, const Json &, std::size_t owner_bound, const ModifierOwnerLookup &,
	             const std::map<std::string, ModifierEffectDefinition> &, const PropertyCatalog &,
	             const ModifierTargetCompatible &, const PropertyMutationObserver &);

private:
	flecs::query<const ActiveEffectIdentity, const EffectTarget, const EffectLifetime, const ModifierValues> modifiers_;
	flecs::query<const StableId, const PropertyEffectOwner> owners_;
};
} // namespace phoenix
