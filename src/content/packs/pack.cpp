module;
#include <miniz.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <set>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.content.packs;
import phoenix.contracts;

import phoenix.content.image_codec;

namespace phoenix
{
namespace
{
constexpr std::size_t max_archive = content_archive_limit;
constexpr std::size_t max_file = 16 * 1024 * 1024;
constexpr std::size_t max_total = 64 * 1024 * 1024;
constexpr std::size_t max_compiled_total = 256 * 1024 * 1024;
void archive_size(std::size_t size, ArchivePurpose purpose)
{
	if (size > (purpose == ArchivePurpose::Save ? runtime_save_archive_limit : max_archive))
		throw std::runtime_error(purpose == ArchivePurpose::Save ? "save: archive exceeds 128 MiB limit"
		                                                         : "pack: archive exceeds 64 MiB limit");
}
void archive_purpose(const Json &manifest, ArchivePurpose purpose)
{
	const auto format = manifest.value("format", std::string{});
	if (purpose == ArchivePurpose::Save && format != "phoenix-runtime-save" && format != "phoenix-fleet-setup")
		throw std::runtime_error("save: expected a runtime save or fleet setup envelope");
}
constexpr std::string_view dat_magic = "PHXDAT1\n", json_magic = "PHXCBR1\n", webp_magic = "PHXWEBP1";
bool signature(std::span<const std::uint8_t> bytes, std::string_view magic)
{
	return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
}
Bytes binary_json(const Json &json)
{
	Bytes result(json_magic.begin(), json_magic.end());
	const auto payload = Json::to_cbor(json);
	result.insert(result.end(), payload.begin(), payload.end());
	return result;
}
void path_check(const std::string &path)
{
	if (path.size() >= MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE)
		throw std::runtime_error("pack: entry path exceeds supported length");
	if (path.empty() || path.front() == '/' || path.back() == '/' ||
	    path.find_first_of("\\:\0", 0, 3) != std::string::npos)
		throw std::runtime_error("pack: unsafe path '" + path + "'");
	std::size_t start = 0;
	while (start < path.size())
	{
		auto end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		auto segment = path.substr(start, end - start);
		if (segment.empty() || segment == "." || segment == "..")
			throw std::runtime_error("pack: unsafe path '" + path + "'");
		start = end + 1;
	}
}
void validate_manifest(const Json &manifest)
{
	if (!manifest.is_object() || manifest.value("schema", 0) != 1 || !manifest.contains("id") ||
	    !manifest["id"].is_string())
		throw std::runtime_error("manifest.json: expected schema 1 and string id");
	auto id = manifest["id"].get<std::string>();
	if (id.empty() || id.size() > 128 ||
	    id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
		throw std::runtime_error("manifest.json: invalid pack id '" + id + "'");
	if (manifest.contains("parent"))
	{
		const auto parent = manifest.at("parent").get<std::string>();
		if (parent.empty() || parent == id || parent.size() > 128 ||
		    parent.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos ||
		    manifest.contains("base") || manifest.contains("dependencies"))
			throw std::runtime_error(
			    "manifest.json: child requires one valid parent and cannot declare base/dependencies");
	}
	if (manifest.contains("base"))
	{
		const auto &base = manifest.at("base");
		if (!base.is_object() || manifest.contains("dependencies"))
			throw std::runtime_error("manifest.json: base must be an independent root");
		const auto mode = base.value("render_mode", std::string("sprites"));
		if (mode != "sprites" && mode != "models")
			throw std::runtime_error("manifest.json: unsupported base render_mode");
		if (base.value("interface_style", std::string("current")) != "current")
			throw std::runtime_error("manifest.json: unsupported interface_style");
	}
	for (auto key : {"dependencies", "overrides"})
	{
		if (manifest.contains(key))
		{
			if (!manifest[key].is_array())
				throw std::runtime_error(std::string("manifest.json: ") + key + " must be an array");
			std::set<std::string> seen;
			for (const auto &item : manifest[key])
				if (!item.is_string() || !seen.insert(item.get<std::string>()).second)
					throw std::runtime_error(std::string("manifest.json: ") + key +
					                         " contains a duplicate/non-string id");
		}
	}
}
} // namespace
struct Pack::Storage
{
	PackDraft content;
	std::string hash;
	Bytes archive;
};
Pack::Pack(std::shared_ptr<const Storage> storage) : storage_(std::move(storage))
{
}
const Json &Pack::manifest() const
{
	return storage_->content.manifest;
}
const std::map<std::string, Bytes> &Pack::files() const
{
	return storage_->content.files;
}
const std::string &Pack::hash() const
{
	return storage_->hash;
}
const Bytes &Pack::archive() const
{
	return storage_->archive;
}
std::shared_ptr<const Bytes> Pack::archive_owner() const
{
	return {storage_, &storage_->archive};
}
PackDraft Pack::draft() const
{
	auto result = storage_->content;
	for (auto &[path, bytes] : result.files)
	{
		if (signature(bytes, json_magic))
			bytes = json_bytes(parse_json(bytes, path));
		else if (compiled_image(bytes))
			bytes = source_asset_image(bytes);
	}
	return result;
}
Bytes json_bytes(const Json &json)
{
	auto text = json.dump(2);
	return Bytes(text.begin(), text.end());
}
Json parse_json(std::span<const std::uint8_t> bytes, const std::string &source)
{
	try
	{
		if (signature(bytes, json_magic))
			return Json::from_cbor(bytes.begin() + json_magic.size(), bytes.end());
		std::vector<std::set<std::string>> objects;
		return Json::parse(bytes.begin(), bytes.end(),
		                   [&](int, Json::parse_event_t event, Json &value)
		                   {
			                   if (event == Json::parse_event_t::object_start)
				                   objects.emplace_back();
			                   else if (event == Json::parse_event_t::key &&
			                            !objects.back().insert(value.get<std::string>()).second)
				                   throw std::runtime_error("duplicate object key '" + value.get<std::string>() + "'");
			                   else if (event == Json::parse_event_t::object_end)
				                   objects.pop_back();
			                   return true;
		                   });
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(source + ": " + error.what());
	}
}
Pack read_pack(std::span<const std::uint8_t> bytes, ArchivePurpose purpose)
{
	archive_size(bytes.size(), purpose);
	return read_pack(Bytes(bytes.begin(), bytes.end()), purpose);
}
Pack read_pack(Bytes &&archive, ArchivePurpose purpose)
{
	archive_size(archive.size(), purpose);
	std::span<const std::uint8_t> bytes(archive);
	const bool compiled = signature(bytes, dat_magic);
	if (!compiled && signature(bytes, "PHXDAT"))
		throw std::runtime_error("pack: unsupported DAT version");
	if (compiled)
		bytes = bytes.subspan(dat_magic.size());
	const auto entry_limit = compiled && purpose == ArchivePurpose::Content ? 64 * 1024 * 1024 + 16 : max_file;
	const auto total_limit = compiled || purpose == ArchivePurpose::Save ? max_compiled_total : max_total;
	archive_size(bytes.size(), purpose);
	mz_zip_archive zip{};
	if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
		throw std::runtime_error("pack: invalid ZIP");
	struct End
	{
		mz_zip_archive *zip;
		~End()
		{
			mz_zip_reader_end(zip);
		}
	} end{&zip};
	auto storage = std::make_shared<Pack::Storage>();
	auto &result = storage->content;
	storage->hash = sha256(archive);
	auto count = mz_zip_reader_get_num_files(&zip);
	if (count > 1024)
		throw std::runtime_error("pack: more than 1024 entries");
	std::size_t total = 0;
	for (mz_uint i = 0; i < count; ++i)
	{
		mz_zip_archive_file_stat stat{};
		if (!mz_zip_reader_file_stat(&zip, i, &stat))
			throw std::runtime_error("pack: cannot read ZIP directory");
		if (mz_zip_reader_get_filename(&zip, i, nullptr, 0) > sizeof(stat.m_filename))
			throw std::runtime_error("pack: entry path exceeds supported length");
		if (stat.m_is_directory)
			continue;
		std::string path = stat.m_filename;
		path_check(path);
		if (stat.m_is_encrypted || stat.m_uncomp_size > entry_limit || stat.m_uncomp_size > total_limit - total)
			throw std::runtime_error(path + ": encrypted or oversized pack entry");
		if (result.files.contains(path))
			throw std::runtime_error(path + ": duplicate ZIP entry");
		Bytes file(static_cast<std::size_t>(stat.m_uncomp_size));
		if (!mz_zip_reader_extract_to_mem(&zip, i, file.data(), file.size(), 0))
			throw std::runtime_error(path + ": extraction/CRC failed");
		if (path.ends_with(".png") && signature(file, "PHX") && !compiled_image(file))
			throw std::runtime_error(path + ": unsupported compiled image version; rebuild the DAT");
		total += file.size();
		result.files.emplace(path, std::move(file));
	}
	if (!result.files.contains("manifest.json") || !result.files.contains("definitions.json"))
		throw std::runtime_error("pack: missing manifest.json or definitions.json");
	result.manifest = parse_json(result.files.at("manifest.json"), "manifest.json");
	validate_manifest(result.manifest);
	archive_purpose(result.manifest, purpose);
	storage->archive = std::move(archive);
	return Pack(std::move(storage));
}
void verify_pack_draft(const PackDraft &pack, ArchivePurpose purpose)
{
	validate_manifest(pack.manifest);
	archive_purpose(pack.manifest, purpose);
	const auto total_limit = purpose == ArchivePurpose::Save ? max_compiled_total : max_total;
	if (!pack.files.contains("definitions.json"))
		throw std::runtime_error("pack: missing definitions.json");
	if (pack.files.size() + (pack.files.contains("manifest.json") ? 0 : 1) > 1024)
		throw std::runtime_error("pack: more than 1024 entries");
	std::size_t total = json_bytes(pack.manifest).size();
	if (total > max_file)
		throw std::runtime_error("manifest.json: oversized pack entry");
	for (const auto &[path, bytes] : pack.files)
	{
		path_check(path);
		if (path == "manifest.json")
			continue;
		if (path.ends_with(".png") && signature(bytes, "PHX") && !compiled_image(bytes))
			throw std::runtime_error(path + ": unsupported compiled image version; rebuild the DAT");
		if (bytes.size() > max_file || bytes.size() > total_limit - total)
			throw std::runtime_error(path + ": oversized pack entry");
		total += bytes.size();
	}
}
Bytes write_pack(const PackDraft &source, PackEncoding encoding, ArchivePurpose purpose)
{
	PackDraft pack = source;
	// Re-exporting compiled input to ZIP restores editable JSON and PNG sources.
	for (auto &[path, bytes] : pack.files)
	{
		if (signature(bytes, json_magic))
			bytes = json_bytes(parse_json(bytes, path));
		else if (encoding == PackEncoding::Zip && compiled_image(bytes))
			bytes = source_asset_image(bytes);
		else if (compiled_image(bytes))
			(void)decode_asset_image(bytes);
	}
	verify_pack_draft(pack, purpose);
	if (encoding == PackEncoding::Dat)
	{
		std::size_t total = 0;
		for (auto &[path, bytes] : pack.files)
		{
			if (path == "definitions.json")
				bytes = binary_json(parse_json(bytes, path));
			else if (bytes.size() >= 4 && bytes[0] == 137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71)
				bytes = compile_asset_image(bytes);
			if (bytes.size() > max_compiled_total - total)
				throw std::runtime_error("pack: compiled images exceed 256 MiB");
			total += bytes.size();
		}
	}
	mz_zip_archive zip{};
	if (!mz_zip_writer_init_heap(&zip, 0, 0))
		throw std::runtime_error("pack: cannot initialise ZIP writer");
	struct End
	{
		mz_zip_archive *zip;
		~End()
		{
			mz_zip_writer_end(zip);
		}
	} end{&zip};
	auto add = [&](const std::string &path, const Bytes &bytes)
	{
		if (!mz_zip_writer_add_mem(&zip, path.c_str(), bytes.data(), bytes.size(),
		                           signature(bytes, webp_magic) ? MZ_NO_COMPRESSION : MZ_BEST_SPEED))
			throw std::runtime_error(path + ": ZIP write failed");
	};
	// Preserve the original sorted entry order without copying every asset buffer.
	const auto manifest = json_bytes(pack.manifest);
	bool manifest_added = false;
	for (const auto &[path, bytes] : pack.files)
	{
		if (!manifest_added && path >= "manifest.json")
		{
			add("manifest.json", manifest);
			manifest_added = true;
		}
		if (path != "manifest.json")
			add(path, bytes);
	}
	if (!manifest_added)
		add("manifest.json", manifest);
	void *output = nullptr;
	std::size_t size = 0;
	if (!mz_zip_writer_finalize_heap_archive(&zip, &output, &size))
		throw std::runtime_error("pack: ZIP finalise failed");
	std::unique_ptr<void, decltype(&mz_free)> allocation(output, &mz_free);
	archive_size(size, purpose);
	auto *begin = static_cast<std::uint8_t *>(output);
	Bytes result;
	if (encoding == PackEncoding::Dat)
		result.assign(dat_magic.begin(), dat_magic.end());
	result.insert(result.end(), begin, begin + size);
	archive_size(result.size(), purpose);
	return result;
}
Pack read_pack_path(const std::string &path)
{
	const auto root = std::filesystem::u8path(path);
	if (!std::filesystem::is_directory(root))
		return read_pack(read_file(path));
	PackDraft draft;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
	{
		if (entry.is_symlink())
			throw std::runtime_error("pack: source symlinks are not supported");
		if (!entry.is_regular_file())
			continue;
		if (entry.file_size() > max_file || draft.files.size() >= 1024)
			throw std::runtime_error("pack: source limits exceeded");
		const auto name = std::filesystem::relative(entry.path(), root).generic_string();
		const auto encoded = entry.path().u8string();
		draft.files[name] = read_file(std::string(encoded.begin(), encoded.end()));
		std::size_t total = 0;
		for (const auto &[key, data] : draft.files)
			total += data.size();
		if (total > max_total)
			throw std::runtime_error("pack: source exceeds 64 MiB");
	}
	draft.manifest = parse_json(draft.files.at("manifest.json"), "manifest.json");
	return read_pack(write_pack(draft));
}
Bytes distribution_zip(const std::map<std::string, Bytes> &files)
{
	mz_zip_archive zip{};
	if (!mz_zip_writer_init_heap(&zip, 0, 0))
		throw std::runtime_error("Cannot create distribution ZIP");
	struct End
	{
		mz_zip_archive *zip;
		~End()
		{
			mz_zip_writer_end(zip);
		}
	} end{&zip};
	std::size_t total = 0;
	if (files.size() > 1024)
		throw std::runtime_error("Too many companion files");
	for (const auto &[path, bytes] : files)
	{
		path_check(path);
		if (bytes.size() > max_archive || bytes.size() > max_compiled_total - total)
			throw std::runtime_error("Distribution exceeds size limits");
		total += bytes.size();
		if (!mz_zip_writer_add_mem(&zip, path.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED))
			throw std::runtime_error("Cannot write distribution file");
	}
	void *output = nullptr;
	std::size_t size = 0;
	if (!mz_zip_writer_finalize_heap_archive(&zip, &output, &size))
		throw std::runtime_error("Cannot finish distribution ZIP");
	std::unique_ptr<void, decltype(&mz_free)> allocation(output, &mz_free);
	return Bytes(static_cast<std::uint8_t *>(output), static_cast<std::uint8_t *>(output) + size);
}
void write_companions(const std::string &archive_path, const std::map<std::string, Bytes> &files)
{
	const auto root = std::filesystem::u8path(archive_path).parent_path();
	for (const auto &[name, bytes] : files)
	{
		path_check(name);
		const auto target = root / std::filesystem::u8path(name);
		std::filesystem::create_directories(target.parent_path());
		const auto encoded = target.u8string();
		write_file(std::string(encoded.begin(), encoded.end()), bytes);
	}
}
} // namespace phoenix
