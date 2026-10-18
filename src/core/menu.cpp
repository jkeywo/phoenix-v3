module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.menu;
namespace phoenix
{
MenuLibrary::MenuLibrary(std::string directory) : directory_(std::move(directory))
{
}
std::string MenuLibrary::path(const std::string &folder, const std::string &id, const std::string &extension) const
{
	if (!valid_hex_token(id))
		throw std::runtime_error("Invalid library entry identity");
	const auto value = (std::filesystem::u8path(directory_) / folder / (id + extension)).u8string();
	return {value.begin(), value.end()};
}
void MenuLibrary::atomic_write(const std::string &filename, const Bytes &bytes) const
{
	auto file = std::filesystem::u8path(filename);
	std::filesystem::create_directories(file.parent_path());
	auto temporary = file;
	temporary += "." + random_token() + ".tmp";
	const auto encoded = temporary.u8string();
	try
	{
		write_file(std::string(encoded.begin(), encoded.end()), bytes);
#ifdef _WIN32
		// The platform replacement below is supplied without removing the original first.
		if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("Cannot replace native library file");
#else
		std::filesystem::rename(temporary, file);
#endif
	}
	catch (...)
	{
		std::error_code error;
		std::filesystem::remove(temporary, error);
		throw;
	}
}
void MenuLibrary::load()
{
	const auto filename = std::filesystem::u8path(directory_) / "library.json";
	if (!std::filesystem::exists(filename))
		return;
	writable_ = false;
	const auto encoded = filename.u8string();
	const auto data = parse_json(read_file(std::string(encoded.begin(), encoded.end()), 1024 * 1024), "native library");
	if (data.at("schema") != 1 || data.at("packs").size() > 256 || data.at("saves").size() > 4096)
		throw std::runtime_error("Unsupported native library index");
	MenuPreferences prefs;
	prefs.volume = data.at("volume");
	prefs.language = data.at("language");
	prefs.pseudo = data.value("pseudo", false);
	prefs.reduced_motion = data.value("reduced_motion", false);
	if (prefs.volume < 0 || prefs.volume > 100 || prefs.language.empty() || prefs.language.size() > 32)
		throw std::runtime_error("Invalid native preferences");
	std::vector<LibraryPack> installed;
	std::vector<ManagedSave> saved;
	for (const auto &p : data.at("packs"))
	{
		LibraryPack pack{p.at("hash"), p.at("id"), p.at("enabled")};
		path("packs", pack.hash, ".zip");
		if (pack.id.size() > 256 || std::any_of(installed.begin(), installed.end(),
		                                        [&](const auto &v)
		                                        {
			                                        return v.hash == pack.hash;
		                                        }))
			throw std::runtime_error("Invalid native pack index");
		installed.push_back(std::move(pack));
	}
	for (const auto &s : data.at("saves"))
	{
		ManagedSave save{s.at("id"), s.at("name"), s.at("scenario"), s.at("saved_at")};
		path("saves", save.id, ".zip");
		if (save.name.size() > 256 || save.scenario.size() > 256 || save.saved_at.size() > 64)
			throw std::runtime_error("Invalid managed save metadata");
		saved.push_back(std::move(save));
	}
	preferences = std::move(prefs);
	packs = std::move(installed);
	saves = std::move(saved);
	writable_ = true;
}
void MenuLibrary::remember()
{
	if (!writable_)
		throw std::runtime_error(
		    "Native library index could not be loaded; preserve and repair library.json before saving changes");
	Json data{{"schema", 1},
	          {"volume", preferences.volume},
	          {"language", preferences.language},
	          {"pseudo", preferences.pseudo},
	          {"reduced_motion", preferences.reduced_motion},
	          {"packs", Json::array()},
	          {"saves", Json::array()}};
	for (const auto &p : packs)
		data["packs"].push_back({{"hash", p.hash}, {"id", p.id}, {"enabled", p.enabled}});
	for (const auto &s : saves)
		data["saves"].push_back({{"id", s.id}, {"name", s.name}, {"scenario", s.scenario}, {"saved_at", s.saved_at}});
	const auto encoded = (std::filesystem::u8path(directory_) / "library.json").u8string();
	atomic_write(std::string(encoded.begin(), encoded.end()), json_bytes(data));
}
void MenuLibrary::install(const Bytes &archive)
{
	const auto pack = read_pack(archive);
	const auto format = pack.manifest().value("format", std::string{});
	if (!format.empty() && format != "phoenix-mod-pack")
		throw std::runtime_error("Import gameplay packs here; use Saved Games for runtime saves");
	if (std::any_of(packs.begin(), packs.end(),
	                [&](const auto &p)
	                {
		                return p.hash == pack.hash();
	                }))
		return;
	if (packs.size() >= 256)
		throw std::runtime_error("Native pack library is full");
	atomic_write(path("packs", pack.hash(), ".zip"), archive);
	packs.push_back({pack.hash(), pack.manifest().at("id"), false});
	try
	{
		remember();
	}
	catch (...)
	{
		packs.pop_back();
		throw;
	}
}
std::vector<Bytes> MenuLibrary::archives(const std::vector<LibraryPack> &selection) const
{
	std::vector<Bytes> result;
	std::vector<Pack> loaded;
	for (const auto &p : selection)
		if (p.enabled)
		{
			auto bytes = read_file(path("packs", p.hash, ".zip"));
			auto pack = read_pack(bytes);
			if (pack.hash() != p.hash)
				throw std::runtime_error("Installed pack failed its integrity check: " + p.id);
			loaded.push_back(std::move(pack));
			result.push_back(std::move(bytes));
		}
	if (loaded.size() > 32)
		throw std::runtime_error("Enable at most 32 ordered packs");
	if (!loaded.empty())
		resolve_packs(loaded);
	return result;
}
void MenuLibrary::apply(const std::vector<LibraryPack> &selection)
{
	archives(selection);
	auto previous = packs;
	packs = selection;
	try
	{
		remember();
	}
	catch (...)
	{
		packs = std::move(previous);
		throw;
	}
	for (const auto &old : previous)
		if (std::none_of(packs.begin(), packs.end(),
		                 [&](const auto &p)
		                 {
			                 return p.hash == old.hash;
		                 }))
		{
			std::error_code error;
			std::filesystem::remove(std::filesystem::u8path(path("packs", old.hash, ".zip")), error);
		}
}
ManagedSave MenuLibrary::store_save(const Bytes &archive, const std::string &name, const std::string &scenario)
{
	const auto pack = read_pack(archive);
	const auto format = pack.manifest().value("format", std::string{});
	if (format != "phoenix-runtime-save" && format != "phoenix-fleet-setup")
		throw std::runtime_error("Select a Phoenix runtime save");
	if (saves.size() >= 4096 || name.size() > 256 || scenario.size() > 256)
		throw std::runtime_error("Managed save metadata exceeds bounds");
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	std::ostringstream stamp;
	stamp << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
	const auto mission =
	    scenario.empty() ? parse_json(pack.files().at("session.json"), "save metadata").at("mission").get<std::string>()
	                     : scenario;
	ManagedSave save{random_token(), name, mission, stamp.str()};
	atomic_write(path("saves", save.id, ".zip"), archive);
	saves.insert(saves.begin(), save);
	try
	{
		remember();
	}
	catch (...)
	{
		saves.erase(saves.begin());
		throw;
	}
	return save;
}
Bytes MenuLibrary::read_save(const std::string &id) const
{
	if (std::none_of(saves.begin(), saves.end(),
	                 [&](const auto &s)
	                 {
		                 return s.id == id;
	                 }))
		throw std::runtime_error("Unknown managed save");
	return read_file(path("saves", id, ".zip"));
}
} // namespace phoenix
