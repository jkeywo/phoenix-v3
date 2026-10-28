module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay.combat.shields;

namespace phoenix
{
const ShieldContributionDefinition &shield_contribution(const ShieldEffectDefinition &definition,
                                                        const std::string &name)
{
	const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
	                                [&](const auto &d)
	                                {
		                                return d.name == name;
	                                });
	if (entry == definition.contributions.end())
		throw std::runtime_error("Unknown shield contribution");
	return *entry;
}

std::uint64_t install_shield_grant(ShieldComponent &owner, const ShieldEffectDefinition &definition,
                                   const std::string &source, const std::string &equipment,
                                   const std::string &definition_id, std::int64_t expiry)
{
	validate_effect_identity(source, equipment, false);
	if (expiry < -1 || expiry > 9007199254740991LL)
		throw std::runtime_error("Effect expiry outside supported bounds");
	const auto count = definition.contributions.size();
	if (!count)
		throw std::runtime_error("Shield grant has no compiled contributions");
	if (owner.contributions.size() + count > 8192 || owner.next_effect >= 9007199254740991ULL)
		throw std::runtime_error("Shield grant capacity exhausted");
	const auto id = owner.next_effect;
	std::vector<ShieldContribution> prepared;
	prepared.reserve(count);
	for (const auto &entry : definition.contributions)
	{
		ShieldContribution c;
		c.effect_id = id;
		c.source = source;
		c.equipment = equipment;
		c.definition_id = definition_id;
		c.definition = entry;
		c.state.strength = entry.shield.reserve.initial;
		c.expiry = expiry;
		prepared.push_back(std::move(c));
	}
	owner.contributions.reserve(owner.contributions.size() + prepared.size());
	for (auto &c : prepared)
		owner.contributions.push_back(std::move(c));
	++owner.next_effect;
	return id;
}
void expire_shield_grants(ShieldComponent &owner, std::int64_t tick)
{
	std::erase_if(owner.contributions,
	              [&](const auto &contribution)
	              {
		              return contribution.expiry >= 0 && tick >= contribution.expiry;
	              });
}
} // namespace phoenix
