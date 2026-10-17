module;
#include <cstdint>
#include <cstddef>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module phoenix.data;

export namespace phoenix
{
using Json = nlohmann::json;
using WireJson = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
std::string sha256(std::span<const std::uint8_t> bytes);
Bytes read_file(const std::string &path, std::size_t max_bytes = 32 * 1024 * 1024);
void write_file(const std::string &path, std::span<const std::uint8_t> bytes);
std::string random_token();
// Protocol/storage identities and SHA-256/secure tokens have deliberately
// separate rules from authored pack identifiers.
bool valid_identifier(std::string_view value);
bool valid_hex_token(std::string_view value);
} // namespace phoenix
