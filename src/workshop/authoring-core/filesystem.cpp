module;
#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;

namespace phoenix
{
namespace
{
std::string utf8(const std::filesystem::path &p)
{
	const auto s = p.generic_u8string();
	return {s.begin(), s.end()};
}
void safe_destination(const std::filesystem::path &root, const std::filesystem::path &relative)
{
	if (relative.empty() || relative.is_absolute() || relative.has_root_path())
		throw std::runtime_error("Expected a relative pack path");
	auto current = root;
	for (const auto &part : relative)
	{
		if (part == "." || part == ".." || utf8(part).find_first_of(":\\") != std::string::npos)
			throw std::runtime_error("Unsafe pack path: " + utf8(relative));
		current /= part;
		if (std::filesystem::is_symlink(std::filesystem::symlink_status(current)))
			throw std::runtime_error("Pack output must not traverse symbolic links: " + utf8(current));
	}
}
} // namespace
PackDraft workshop_read_source(const std::string &path)
{
	const auto root = std::filesystem::u8path(path);
	if (!std::filesystem::is_directory(root))
		throw std::runtime_error("Source directory does not exist: " + path);
	PackDraft result;
	result.manifest = parse_json(read_file(utf8(root / "manifest.json"), 16 * 1024 * 1024), "manifest.json");
	// Let production pack verification enforce the cumulative source limits.
	result.files["definitions.json"] = {};
	bool found_definitions = false;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
	{
		if (entry.is_symlink())
			throw std::runtime_error("Pack sources must not contain symbolic links");
		if (!entry.is_regular_file())
			continue;
		if (entry.file_size() > 16 * 1024 * 1024)
			throw std::runtime_error("Pack source exceeds entry/size limits");
		const auto name = utf8(entry.path().lexically_relative(root));
		found_definitions = found_definitions || name == "definitions.json";
		result.files[name] = read_file(utf8(entry.path()));
		verify_pack_draft(result);
	}
	if (!found_definitions)
		throw std::runtime_error("Source draft is missing definitions.json");
	verify_pack_draft(result);
	return result;
}
void workshop_write_files(const std::string &directory, const std::map<std::string, Bytes> &files)
{
	const auto root = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
	// Validate every destination before creating or replacing any output.
	for (auto at = root; !at.empty(); at = at.parent_path())
	{
		if (std::filesystem::is_symlink(std::filesystem::symlink_status(at)))
			throw std::runtime_error("Output directory must not traverse symbolic links");
		if (at == at.parent_path())
			break;
	}
	for (const auto &[name, bytes] : files)
		safe_destination(root, std::filesystem::u8path(name));
	struct Entry
	{
		std::filesystem::path path;
		bool existed;
		Bytes previous;
		StagedFileReplacement replacement;
	};
	std::vector<Entry> staged;
	for (const auto &[name, bytes] : files)
	{
		const auto path = root / std::filesystem::u8path(name);
		std::filesystem::create_directories(path.parent_path());
		const bool existed = std::filesystem::exists(path);
		auto previous = existed ? read_file(utf8(path)) : Bytes{};
		staged.push_back({path, existed, std::move(previous),
		                  StagedFileReplacement(utf8(path), bytes, FileWriteMode::Flushed, "Workshop output")});
	}
	std::size_t committed = 0;
	try
	{
		for (auto &entry : staged)
		{
			entry.replacement.commit();
			++committed;
		}
	}
	catch (const std::exception &error)
	{
		const std::string original = error.what();
		std::string rollback;
		while (committed)
		{
			auto &entry = staged[--committed];
			try
			{
				if (entry.existed)
				{
					StagedFileReplacement restore(utf8(entry.path), entry.previous, FileWriteMode::Flushed,
					                              "Workshop rollback");
					restore.commit();
				}
				else
					std::filesystem::remove(entry.path);
			}
			catch (const std::exception &failure)
			{
				rollback += " Restore failed: " + std::string(failure.what());
			}
		}
		throw std::runtime_error(original + rollback);
	}
}
void workshop_save_source(const WorkshopDocument &document, const std::string &path)
{
	auto draft = document.source_draft();
	verify_pack_draft(draft);
	draft.files["manifest.json"] = json_bytes(draft.manifest);
	workshop_write_files(path, draft.files);
}
void workshop_export(const WorkshopDocument &document, const std::string &path, bool scenario_only)
{
	const auto destination = std::filesystem::u8path(path);
	const auto extension = destination.extension();
	if (extension != ".zip" && extension != ".dat")
		throw std::runtime_error("Package output must end in .zip or .dat");
	const auto files = document.export_companions(
	    utf8(destination.filename()), extension == ".dat" ? PackEncoding::Dat : PackEncoding::Zip, scenario_only);
	workshop_write_files(destination.has_parent_path() ? utf8(destination.parent_path()) : ".", files);
}
} // namespace phoenix
