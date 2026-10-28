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

export module phoenix.effects.grants;
export import phoenix.effects.rule_state;

export namespace phoenix
{
void prepare_active_rule_grant(std::vector<ActiveRuleEffect> &, std::uint64_t &next, const EffectOperation &,
                               const std::string &source, EntityId original_source, const PayloadHitContext &,
                               const std::string &group_source, const std::string &original_equipment,
                               const PeriodicEffectDefinition *, std::int64_t tick,
                               const NativeRuleContext *origin = nullptr);
} // namespace phoenix
