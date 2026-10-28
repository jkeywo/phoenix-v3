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

module phoenix.content.equipment.types;

namespace phoenix
{
void FactionCatalog::validate(const std::string &id) const
{
	if (!id.empty() && !labels.contains(id))
		throw std::runtime_error("Unknown map faction: " + id);
}

std::string FactionCatalog::relationship(const std::string &a, const std::string &b) const
{
	if (a.empty() || b.empty())
		return "neutral";
	if (a == b)
		return "friendly";
	const auto key = std::minmax(a, b);
	const auto found = relationships.find({key.first, key.second});
	return found == relationships.end() ? "neutral" : found->second;
}
} // namespace phoenix
