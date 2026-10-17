module;
#include <miniz.h>
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
	return storage_->content;
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
	const std::span<const std::uint8_t> bytes(archive);
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
	storage->hash = sha256(bytes);
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
		if (stat.m_is_encrypted || stat.m_uncomp_size > max_file || stat.m_uncomp_size > max_total - total)
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
Bytes write_pack(const PackDraft &pack)
{
	verify_pack_draft(pack);
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
	return Bytes(begin, begin + size);
}
} // namespace phoenix
