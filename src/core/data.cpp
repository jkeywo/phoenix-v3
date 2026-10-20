module;
#include <picosha2.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module phoenix.data;

namespace phoenix
{
std::string sha256(std::span<const std::uint8_t> bytes)
{
	return picosha2::hash256_hex_string(bytes.begin(), bytes.end());
}
Bytes read_file(const std::string &path, std::size_t max_bytes)
{
	std::ifstream input(std::filesystem::path(std::u8string(path.begin(), path.end())),
	                    std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Cannot open '" + path + "'");
	auto length = input.tellg();
	if (length < 0 || static_cast<std::uintmax_t>(length) > max_bytes)
		throw std::runtime_error(path + ": file exceeds " + std::to_string(max_bytes) + " byte limit");
	Bytes result(static_cast<std::size_t>(length));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char *>(result.data()), static_cast<std::streamsize>(result.size())))
		throw std::runtime_error(path + ": read failed");
	return result;
}
void write_file(const std::string &path, std::span<const std::uint8_t> bytes)
{
	std::ofstream output(std::filesystem::path(std::u8string(path.begin(), path.end())),
	                     std::ios::binary | std::ios::trunc);
	if (!output ||
	    !output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
		throw std::runtime_error(path + ": write failed");
}
bool valid_identifier(std::string_view value)
{
	return !value.empty() && value.size() <= 128 &&
	       value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") ==
	           std::string_view::npos;
}
bool valid_hex_token(std::string_view value)
{
	return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
std::int64_t checked_integer(const Json &source, std::int64_t minimum, std::int64_t maximum, const std::string &path)
{
	if (!source.is_number_integer())
		throw std::runtime_error(path + ": expected integer");
	if (source.is_number_unsigned() &&
	    source.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
		throw std::runtime_error(path + ": integer outside supported range");
	const auto value = source.get<std::int64_t>();
	if (value < minimum || value > maximum)
		throw std::runtime_error(path + ": integer outside supported range");
	return value;
}
std::uint64_t checked_unsigned(const Json &source, std::uint64_t minimum, std::uint64_t maximum,
                               const std::string &path)
{
	if (!source.is_number_integer())
		throw std::runtime_error(path + ": expected unsigned integer");
	if (!source.is_number_unsigned() && source.get<std::int64_t>() < 0)
		throw std::runtime_error(path + ": unsigned integer outside supported range");
	const auto value = source.get<std::uint64_t>();
	if (value < minimum || value > maximum)
		throw std::runtime_error(path + ": unsigned integer outside supported range");
	return value;
}
std::int64_t json_integer(const Json &source, const char *key, std::int64_t fallback, std::int64_t minimum,
                          std::int64_t maximum, const std::string &path)
{
	if (!source.contains(key))
		return fallback;
	return checked_integer(source.at(key), minimum, maximum, path + "." + key);
}
bool json_boolean(const Json &source, const char *key, bool fallback, const std::string &path)
{
	if (!source.contains(key))
		return fallback;
	if (!source.at(key).is_boolean())
		throw std::runtime_error(path + "." + key + ": expected boolean");
	return source.at(key).get<bool>();
}
} // namespace phoenix
