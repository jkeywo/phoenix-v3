module;
#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
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
inline constexpr std::size_t content_archive_limit = 64 * 1024 * 1024;
Bytes read_file(const std::string &path, std::size_t max_bytes = content_archive_limit);
void write_file(const std::string &path, std::span<const std::uint8_t> bytes);
enum class FileWriteMode
{
	Buffered,
	Flushed
};
class StagedFileReplacement
{
public:
	StagedFileReplacement(const std::string &destination, std::span<const std::uint8_t> bytes, FileWriteMode mode,
	                      const std::string &context);
	~StagedFileReplacement();
	StagedFileReplacement(StagedFileReplacement &&) noexcept;
	StagedFileReplacement &operator=(StagedFileReplacement &&) noexcept;
	StagedFileReplacement(const StagedFileReplacement &) = delete;
	StagedFileReplacement &operator=(const StagedFileReplacement &) = delete;
	void commit();

private:
	struct State;
	std::unique_ptr<State> state_;
};
std::string random_token();
// Protocol/storage identities and SHA-256/secure tokens have deliberately
// separate rules from authored pack identifiers.
bool valid_identifier(std::string_view value);
bool valid_hex_token(std::string_view value);
// Read a numeric value without narrowing unsigned JSON integers before validation.
std::int64_t checked_integer(const Json &value, std::int64_t minimum, std::int64_t maximum, const std::string &path);
std::uint64_t checked_unsigned(const Json &value, std::uint64_t minimum, std::uint64_t maximum,
                               const std::string &path);
// Optional typed fields; preserve caller-supplied defaults, ranges and path diagnostics.
std::int64_t json_integer(const Json &source, const char *key, std::int64_t fallback, std::int64_t minimum,
                          std::int64_t maximum, const std::string &path);
bool json_boolean(const Json &source, const char *key, bool fallback, const std::string &path);
} // namespace phoenix
