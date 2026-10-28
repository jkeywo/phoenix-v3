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

export module phoenix.content.equipment.types;
export import phoenix.data;
export import phoenix.contracts;

export namespace phoenix
{
struct FactionCatalog
{
	std::map<std::string, std::string> labels;
	std::map<std::pair<std::string, std::string>, std::string> relationships;
	std::string relationship(const std::string &a, const std::string &b) const;
	void validate(const std::string &id) const;
};
struct OperatingRequest
{
	OperatingAction action;
	std::int64_t value;
	std::vector<std::int64_t> arguments;
};
} // namespace phoenix
