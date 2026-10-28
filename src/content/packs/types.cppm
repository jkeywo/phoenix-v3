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

export module phoenix.content.packs;
export import phoenix.data;
export import phoenix.content.image_codec;

export namespace phoenix
{
struct PackDraft
{
	Json manifest;
	std::map<std::string, Bytes> files;
};
enum class ArchivePurpose
{
	Content,
	Save
};
inline constexpr std::size_t runtime_save_archive_limit = 128 * 1024 * 1024;
// Loaded packs share immutable decoded content and the exact original archive.
// Editing requires a separate draft; a draft has no transferable archive identity.
class Pack
{
public:
	const Json &manifest() const;
	const std::map<std::string, Bytes> &files() const;
	const std::string &hash() const;
	const Bytes &archive() const;
	std::shared_ptr<const Bytes> archive_owner() const;
	PackDraft draft() const;

private:
	struct Storage;
	std::shared_ptr<const Storage> storage_;
	explicit Pack(std::shared_ptr<const Storage> storage);
	friend Pack read_pack(std::span<const std::uint8_t> bytes, ArchivePurpose purpose);
	friend Pack read_pack(Bytes &&bytes, ArchivePurpose purpose);
};
enum class PackEncoding
{
	Zip,
	Dat
};
// Throws an actionable exception. ZIP is read into bounded memory, never
// extracted to caller-controlled paths on disk.
Pack read_pack(std::span<const std::uint8_t> bytes, ArchivePurpose purpose = ArchivePurpose::Content);
Pack read_pack(Bytes &&bytes, ArchivePurpose purpose = ArchivePurpose::Content);
Bytes write_pack(const PackDraft &pack, PackEncoding encoding = PackEncoding::Zip,
                 ArchivePurpose purpose = ArchivePurpose::Content);
Pack read_pack_path(const std::string &path);
void verify_pack_draft(const PackDraft &pack, ArchivePurpose purpose = ArchivePurpose::Content);
Bytes distribution_zip(const std::map<std::string, Bytes> &files);
void write_companions(const std::string &archive_path, const std::map<std::string, Bytes> &files);
Bytes json_bytes(const Json &json);
Json parse_json(std::span<const std::uint8_t> bytes, const std::string &source);
} // namespace phoenix
