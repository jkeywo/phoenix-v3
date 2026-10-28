module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

module phoenix.content.sensing.types;

namespace phoenix
{
bool PsychicDefinition::has_power(const std::string &ability) const
{
	if (!catalog)
		return false;
	const auto found = catalog->definitions.find(ability);
	return found != catalog->definitions.end() && !found->second.psychic_effect.empty();
}

const PsychicPowerDefinition &PsychicDefinition::power(const std::string &ability) const
{
	if (!catalog)
		throw std::runtime_error("psychic: native psychic catalogue is unavailable");
	return catalog->psychic_profiles.at(catalog->definitions.at(ability).psychic_effect);
}
} // namespace phoenix
