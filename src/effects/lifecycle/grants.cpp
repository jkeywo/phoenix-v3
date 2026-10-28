module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.grants;

namespace phoenix
{
void prepare_active_rule_grant(std::vector<ActiveRuleEffect> &prepared, std::uint64_t &next,
                               const EffectOperation &operation, const std::string &source, EntityId original_source,
                               const PayloadHitContext &context, const std::string &group_source,
                               const std::string &original_equipment, const PeriodicEffectDefinition *definition,
                               std::int64_t tick, const NativeRuleContext *origin)
{
	validate_effect_identity(source, {}, false);
	const auto matches = [&](const auto &effect)
	{
		return operation.all_sources
		           ? effect.definition == operation.effect && effect.context.target == context.target &&
		                 effect.context.ordnance == context.ordnance
		           : effect.source == source && same_payload_context(effect.context, context) &&
		                 (operation.effect.empty() || effect.definition == operation.effect);
	};
	if (operation.kind == EffectOperationKind::Remove)
	{
		std::erase_if(prepared, matches);
		return;
	}
	if (!definition)
		throw std::runtime_error("Active rule definition unavailable");
	const auto expiry = effect_expiry(tick, definition->duration_ticks);
	const auto found = std::find_if(prepared.begin(), prepared.end(), matches);
	if (found != prepared.end())
	{
		found->expiry = std::max(found->expiry, expiry);
		if (origin)
		{
			found->origin = *origin;
			found->origin.capture_owner = false;
		}
		return;
	}
	if (prepared.size() >= rule_instance_limit || next >= 9007199254740991ULL)
		throw std::runtime_error("Active rule capacity exhausted");
	ActiveRuleEffect effect;
	effect.id = next++;
	effect.source = source;
	effect.definition = operation.effect;
	effect.group_source = group_source;
	effect.context = context;
	effect.expiry = expiry;
	if (origin)
		effect.origin = *origin;
	else
		effect.origin.source = effect.origin.instigator = {original_source, original_equipment};
	effect.origin.owner = effect.origin.subject = effect.origin.target = rule_hit_actor(context);
	effect.origin.hit = context;
	effect.origin.capture_owner = false;
	prepared.push_back(std::move(effect));
}
} // namespace phoenix
