module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.combat.shields;
export import phoenix.gameplay.combat.state;

export namespace phoenix
{
const ShieldContributionDefinition &shield_contribution(const ShieldEffectDefinition &, const std::string &name);
std::uint64_t install_shield_grant(ShieldComponent &, const ShieldEffectDefinition &, const std::string &source,
                                   const std::string &equipment, const std::string &definition_id,
                                   std::int64_t expiry = -1);
void expire_shield_grants(ShieldComponent &, std::int64_t tick);
} // namespace phoenix
