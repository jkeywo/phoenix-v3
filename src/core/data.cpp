module;
#include <picosha2.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
} // namespace phoenix
