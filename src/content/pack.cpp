module;
#include <miniz.h>
#include <algorithm>
#include <filesystem>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <memory>
#include <stdexcept>
#include <set>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.content;

namespace phoenix
{
namespace
{
constexpr std::size_t max_archive = 32 * 1024 * 1024;
constexpr std::size_t max_file = 16 * 1024 * 1024;
constexpr std::size_t max_total = 64 * 1024 * 1024;
constexpr std::size_t max_compiled_total = 256 * 1024 * 1024;
constexpr std::string_view dat_magic = "PHXDAT1\n", json_magic = "PHXCBR1\n", image_magic = "PHXRGBA1";
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
		return Json::parse(bytes.begin(), bytes.end());
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(source + ": " + error.what());
	}
}
Pack read_pack(std::span<const std::uint8_t> bytes)
{
	if (bytes.size() > max_archive)
		throw std::runtime_error("pack: archive exceeds 32 MiB limit");
	return read_pack(Bytes(bytes.begin(), bytes.end()));
}
Pack read_pack(Bytes &&archive)
{
	if (archive.size() > max_archive)
		throw std::runtime_error("pack: archive exceeds 32 MiB limit");
	std::span<const std::uint8_t> bytes(archive);
	const bool compiled = signature(bytes, dat_magic);
	if (!compiled && signature(bytes, "PHXDAT"))
		throw std::runtime_error("pack: unsupported DAT version");
	if (compiled)
		bytes = bytes.subspan(dat_magic.size());
	const auto entry_limit = compiled ? 64 * 1024 * 1024 + 16 : max_file;
	const auto total_limit = compiled ? max_compiled_total : max_total;
	if (bytes.size() > max_archive)
		throw std::runtime_error("pack: archive exceeds 32 MiB limit");
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
		total += file.size();
		result.files.emplace(path, std::move(file));
	}
	if (!result.files.contains("manifest.json") || !result.files.contains("definitions.json"))
		throw std::runtime_error("pack: missing manifest.json or definitions.json");
	result.manifest = parse_json(result.files.at("manifest.json"), "manifest.json");
	validate_manifest(result.manifest);
	storage->archive = std::move(archive);
	return Pack(std::move(storage));
}
void verify_pack_draft(const PackDraft &pack)
{
	validate_manifest(pack.manifest);
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
		if (bytes.size() > max_file || bytes.size() > max_total - total)
			throw std::runtime_error(path + ": oversized pack entry");
		total += bytes.size();
	}
}
Bytes write_pack(const PackDraft &source, PackEncoding encoding)
{
	PackDraft pack = source;
	// Re-exporting compiled input to ZIP restores editable JSON and PNG sources.
	for (auto &[path, bytes] : pack.files)
	{
		if (signature(bytes, json_magic))
			bytes = json_bytes(parse_json(bytes, path));
		else if (compiled_image(bytes))
			bytes = source_asset_image(bytes);
	}
	verify_pack_draft(pack);
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
		if (!mz_zip_writer_add_mem(&zip, path.c_str(), bytes.data(), bytes.size(), MZ_BEST_SPEED))
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
	if (size > max_archive)
		throw std::runtime_error("pack: archive exceeds 32 MiB limit");
	auto *begin = static_cast<std::uint8_t *>(output);
	Bytes result;
	if (encoding == PackEncoding::Dat)
		result.assign(dat_magic.begin(), dat_magic.end());
	result.insert(result.end(), begin, begin + size);
	if (result.size() > max_archive)
		throw std::runtime_error("pack: archive exceeds 32 MiB limit");
	return result;
}
bool compiled_image(std::span<const std::uint8_t> bytes)
{
	return signature(bytes, image_magic);
}
AssetImage decode_asset_image(std::span<const std::uint8_t> bytes)
{
	AssetImage result;
	if (compiled_image(bytes))
	{
		if (bytes.size() < 16)
			throw std::runtime_error("image: truncated compiled pixels");
		auto integer = [&](std::size_t at)
		{
			std::uint32_t value = 0;
			for (unsigned i = 0; i < 4; ++i)
				value |= std::uint32_t(bytes[at + i]) << (i * 8);
			return value;
		};
		const auto w = integer(8), h = integer(12);
		if (!w || !h || w > 8192 || h > 8192 || std::uint64_t(w) * h > 16777216 ||
		    bytes.size() != 16 + std::uint64_t(w) * h * 4)
			throw std::runtime_error("image: invalid or oversized compiled dimensions");
		result.width = static_cast<int>(w);
		result.height = static_cast<int>(h);
		result.rgba.assign(bytes.begin() + 16, bytes.end());
		return result;
	}
	int channels = 0;
	if (bytes.size() > max_file ||
	    !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &result.width, &result.height,
	                           &channels) ||
	    result.width <= 0 || result.height <= 0 || result.width > 8192 || result.height > 8192 ||
	    std::int64_t(result.width) * result.height > 16777216)
		throw std::runtime_error("image: invalid or oversized PNG dimensions");
	auto *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &result.width, &result.height,
	                                     &channels, 4);
	if (!pixels)
		throw std::runtime_error("image: PNG cannot decode");
	result.rgba.assign(pixels, pixels + std::size_t(result.width) * result.height * 4);
	stbi_image_free(pixels);
	return result;
}
Bytes compile_asset_image(std::span<const std::uint8_t> bytes)
{
	const auto image = decode_asset_image(bytes);
	Bytes result(image_magic.begin(), image_magic.end());
	for (const auto dimension : {image.width, image.height})
		for (unsigned i = 0; i < 4; ++i)
			result.push_back(static_cast<std::uint8_t>((dimension >> (i * 8)) & 255));
	result.insert(result.end(), image.rgba.begin(), image.rgba.end());
	return result;
}
Bytes source_asset_image(std::span<const std::uint8_t> bytes)
{
	if (!compiled_image(bytes))
		return Bytes(bytes.begin(), bytes.end());
	const auto image = decode_asset_image(bytes);
	Bytes result;
	if (!stbi_write_png_to_func(
	        [](void *context, void *data, int size)
	        {
		        auto &output = *static_cast<Bytes *>(context);
		        const auto *begin = static_cast<const std::uint8_t *>(data);
		        output.insert(output.end(), begin, begin + size);
	        },
	        &result, image.width, image.height, 4, image.rgba.data(), image.width * 4))
		throw std::runtime_error("image: PNG encoding failed");
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
} // namespace phoenix
