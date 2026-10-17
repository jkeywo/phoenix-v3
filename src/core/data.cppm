module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.data;

export namespace phoenix
{
using Json = nlohmann::json;
using WireJson = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
} // namespace phoenix
