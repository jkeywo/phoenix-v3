module;
#include <algorithm>
#include <map>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.content;
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
FactionCatalog compile_factions(const Json &map)
{
	FactionCatalog result;
	const auto rows = map.value("factions", Json::array());
	if (!rows.is_array() || rows.size() > 64)
		throw std::runtime_error("Map factions must be an array of at most 64 entries");
	for (const auto &row : rows)
	{
		const auto id = row.at("id").get<std::string>();
		const auto label = row.at("label").get<std::string>();
		if (id.empty() || id.size() > 128 || label.empty() || label.size() > 256 ||
		    id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
		        std::string::npos ||
		    !result.labels.emplace(id, label).second)
			throw std::runtime_error("Invalid or duplicate map faction");
	}
	const auto relations = map.value("relationships", Json::array());
	if (!relations.is_array() || relations.size() > 4096)
		throw std::runtime_error("Invalid faction relationship matrix");
	for (const auto &row : relations)
	{
		const auto a = row.at("a").get<std::string>(), b = row.at("b").get<std::string>();
		const auto value = row.at("relation").get<std::string>();
		if (!result.labels.contains(a) || !result.labels.contains(b) ||
		    (value != "friendly" && value != "neutral" && value != "hostile") || (a == b && value != "friendly"))
			throw std::runtime_error("Invalid faction relationship");
		const auto ordered = std::minmax(a, b);
		const auto [entry, inserted] = result.relationships.emplace(std::pair{ordered.first, ordered.second}, value);
		if (!inserted && entry->second != value)
			throw std::runtime_error("Contradictory mutual faction relationship");
	}
	return result;
}
} // namespace phoenix
