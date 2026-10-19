module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.menu;
namespace phoenix
{
std::string native_data_directory()
{
#ifdef _WIN32
	PWSTR folder = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
		throw std::runtime_error("Cannot locate the local application data folder; specify --data explicitly");
	const auto root = std::filesystem::path(folder);
	CoTaskMemFree(folder);
	const auto encoded = (root / "PhoenixV3").u8string();
	return {encoded.begin(), encoded.end()};
#else
	return "userdata";
#endif
}
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
	std::filesystem::create_directories(std::filesystem::u8path(filename).parent_path());
	StagedFileReplacement(filename, bytes, FileWriteMode::Buffered, "Native library file").commit();
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
	prefs.sfx_volume = data.value("sfx_volume", 50);
	prefs.master_volume = data.value("master_volume", 50);
	prefs.language = data.at("language");
	prefs.pseudo = data.value("pseudo", false);
	prefs.reduced_motion = data.value("reduced_motion", false);
	prefs.dust_motes = data.value("dust_motes", true);
	prefs.reference_grid = data.value("reference_grid", true);
	prefs.player_name = data.value("player_name", std::string("<username>"));
	prefs.avatar = data.value("avatar", Bytes{});
	if (prefs.player_name.empty() || prefs.player_name.size() > 64 ||
	    std::any_of(prefs.player_name.begin(), prefs.player_name.end(),
	                [](unsigned char c)
	                {
		                return c < 32 || c == 127;
	                }))
		throw std::runtime_error("Invalid player name");
	verify_player_avatar(prefs.avatar);
	if (prefs.volume < 0 || prefs.volume > 100 || prefs.sfx_volume < 0 || prefs.sfx_volume > 100 ||
	    prefs.master_volume < 0 || prefs.master_volume > 100 || prefs.language.empty() || prefs.language.size() > 32)
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
	          {"sfx_volume", preferences.sfx_volume},
	          {"master_volume", preferences.master_volume},
	          {"language", preferences.language},
	          {"pseudo", preferences.pseudo},
	          {"reduced_motion", preferences.reduced_motion},
	          {"dust_motes", preferences.dust_motes},
	          {"reference_grid", preferences.reference_grid},
	          {"player_name", preferences.player_name},
	          {"avatar", preferences.avatar},
	          {"packs", Json::array()},
	          {"saves", Json::array()}};
	for (const auto &p : packs)
		data["packs"].push_back({{"hash", p.hash}, {"id", p.id}, {"enabled", p.enabled}});
	for (const auto &s : saves)
		data["saves"].push_back({{"id", s.id}, {"name", s.name}, {"scenario", s.scenario}, {"saved_at", s.saved_at}});
	const auto encoded = (std::filesystem::u8path(directory_) / "library.json").u8string();
	atomic_write(std::string(encoded.begin(), encoded.end()), json_bytes(data));
}
void MenuLibrary::install_default(const Bytes &archive)
{
	const auto pack = read_pack(archive);
	const auto id = pack.manifest().at("id").get<std::string>();
	if (std::any_of(packs.begin(), packs.end(),
	                [&](const auto &entry)
	                {
		                return entry.id == id;
	                }))
		return;
	const bool fresh = packs.empty();
	install(archive);
	if (fresh)
	{
		auto selected = packs;
		selected.front().enabled = true;
		apply(selected);
	}
}
void MenuLibrary::install_bundled(const Bytes &base_archive, const Bytes &addon_archive)
{
	const auto base = read_pack(base_archive), addon = read_pack(addon_archive);
	const auto base_id = base.manifest().at("id").get<std::string>(),
	           addon_id = addon.manifest().at("id").get<std::string>();
	install_default(base_archive);
	// Move the old bundled examples out of an edited base without losing authored maps or changes to the examples.
	const auto selected = packs;
	const auto existing_addon = std::find_if(selected.begin(), selected.end(),
	                                         [&](const auto &p)
	                                         {
		                                         return p.id == addon_id;
	                                         });
	const auto source = std::find_if(selected.begin(), selected.end(),
	                                 [&](const auto &p)
	                                 {
		                                 return p.id == base_id;
	                                 });
	const auto installed = read_pack(read_pack_archive(source->hash));
	const auto installed_addon = existing_addon == selected.end() || existing_addon->hash == addon.hash()
	                                 ? addon
	                                 : read_pack(read_pack_archive(existing_addon->hash));
	PackDraft base_draft{installed.manifest(), installed.files()},
	    addon_draft{installed_addon.manifest(), installed_addon.files()};
	auto from = parse_json(base_draft.files.at("definitions.json"), "installed base definitions");
	auto to = parse_json(addon_draft.files.at("definitions.json"), "bundled mod definitions");
	bool moved = false;
	for (const char *kind : {"entities", "maps", "consoles", "trees"})
	{
		if (!to.contains(kind))
			continue;
		for (auto &definition : to.at(kind))
			if (from.contains(kind))
				for (auto it = from[kind].begin(); it != from[kind].end(); ++it)
					if (it->at("id") == definition.at("id"))
					{
						definition = *it;
						from[kind].erase(it);
						moved = true;
						break;
					}
	}
	if (!moved)
	{
		install_default(addon_archive);
		return;
	}
	const auto shipped = parse_json(base.files().at("definitions.json"), "bundled base definitions");
	for (const auto &map : shipped.value("maps", Json::array()))
		if (std::none_of(from["maps"].begin(), from["maps"].end(),
		                 [&](const auto &m)
		                 {
			                 return m.at("id") == map.at("id");
		                 }))
			from["maps"].push_back(map);
	base_draft.files["definitions.json"] = json_bytes(from);
	addon_draft.files["definitions.json"] = json_bytes(to);
	base_draft.files.erase("launcher.json");
	addon_draft.files.erase("launcher.json");
	base_draft.files["launcher.json"] = json_bytes(launcher_catalog(resolve_draft({}, base_draft)));
	const auto migrated_base = write_pack(base_draft);
	addon_draft.files["launcher.json"] =
	    json_bytes(launcher_catalog(resolve_draft({read_pack(migrated_base)}, addon_draft)));
	const auto migrated_addon = write_pack(addon_draft);
	auto candidate = selected;
	const bool addon_enabled = existing_addon == selected.end() ? source->enabled : existing_addon->enabled;
	std::erase_if(candidate,
	              [&](const auto &p)
	              {
		              return p.id == addon_id;
	              });
	const auto index = static_cast<std::size_t>(std::find_if(candidate.begin(), candidate.end(),
	                                                         [&](const auto &p)
	                                                         {
		                                                         return p.id == base_id;
	                                                         }) -
	                                            candidate.begin());
	candidate[index].hash = sha256(migrated_base);
	candidate.insert(candidate.begin() + index + 1, {sha256(migrated_addon), addon_id, addon_enabled});
	atomic_write(path("packs", candidate[index].hash, ".zip"), migrated_base);
	atomic_write(path("packs", candidate[index + 1].hash, ".zip"), migrated_addon);
	apply(candidate);
}
std::vector<LibraryPack> MenuLibrary::toggle_pack(const std::vector<LibraryPack> &selection,
                                                  const std::string &hash) const
{
	auto candidate = selection;
	std::vector<std::vector<std::string>> dependencies;
	for (const auto &p : candidate)
		dependencies.push_back(
		    read_pack(read_pack_archive(p.hash)).manifest().value("dependencies", std::vector<std::string>{}));
	const auto entry = std::find_if(candidate.begin(), candidate.end(),
	                                [&](const auto &p)
	                                {
		                                return p.hash == hash;
	                                });
	if (entry == candidate.end())
		throw std::runtime_error("Select an installed pack");
	const auto index = static_cast<std::size_t>(entry - candidate.begin());
	std::vector<int> visiting(candidate.size());
	std::function<void(std::size_t)> enable = [&](std::size_t i)
	{
		if (visiting[i] == 1)
			throw std::runtime_error("Pack dependencies contain a cycle");
		if (visiting[i] == 2)
			return;
		visiting[i] = 1;
		for (const auto &id : dependencies[i])
		{
			const auto dependency = std::find_if(candidate.begin(), candidate.end(),
			                                     [&](const auto &p)
			                                     {
				                                     return p.id == id;
			                                     });
			if (dependency == candidate.end())
				throw std::runtime_error("Install the required pack: " + id);
			enable(static_cast<std::size_t>(dependency - candidate.begin()));
		}
		candidate[i].enabled = true;
		visiting[i] = 2;
	};
	if (!entry->enabled)
		enable(index);
	else
	{
		entry->enabled = false;
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (std::size_t i = 0; i < candidate.size(); ++i)
				if (candidate[i].enabled)
					for (const auto &id : dependencies[i])
						if (std::none_of(candidate.begin(), candidate.end(),
						                 [&](const auto &p)
						                 {
							                 return p.id == id && p.enabled;
						                 }))
						{
							candidate[i].enabled = false;
							changed = true;
							break;
						}
		}
	}
	std::vector<LibraryPack> ordered;
	std::fill(visiting.begin(), visiting.end(), 0);
	std::function<void(std::size_t)> append = [&](std::size_t i)
	{
		if (visiting[i])
			return;
		visiting[i] = 1;
		if (candidate[i].enabled)
			for (const auto &id : dependencies[i])
				for (std::size_t j = 0; j < candidate.size(); ++j)
					if (candidate[j].id == id && candidate[j].enabled)
						append(j);
		ordered.push_back(candidate[i]);
	};
	for (std::size_t i = 0; i < candidate.size(); ++i)
		append(i);
	return ordered;
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
Bytes MenuLibrary::read_pack_archive(const std::string &hash) const
{
	if (std::none_of(packs.begin(), packs.end(),
	                 [&](const auto &entry)
	                 {
		                 return entry.hash == hash;
	                 }))
		throw std::runtime_error("The edited pack is no longer installed");
	auto bytes = read_file(path("packs", hash, ".zip"));
	if (read_pack(bytes).hash() != hash)
		throw std::runtime_error("Installed pack failed its integrity check");
	return bytes;
}
LibraryPack MenuLibrary::replace_pack(const std::string &hash, const Bytes &archive)
{
	auto selected = packs;
	auto entry = std::find_if(selected.begin(), selected.end(),
	                          [&](const auto &p)
	                          {
		                          return p.hash == hash;
	                          });
	if (entry == selected.end())
		throw std::runtime_error("The edited pack is no longer installed; use Save as");
	const auto pack = read_pack(archive);
	if (pack.manifest().value("format", std::string("phoenix-mod-pack")) != "phoenix-mod-pack")
		throw std::runtime_error("Only gameplay packs can replace an installed pack");
	LibraryPack replacement{pack.hash(), pack.manifest().at("id"), entry->enabled};
	if (std::any_of(selected.begin(), selected.end(),
	                [&](const auto &p)
	                {
		                return p.hash != hash && (p.hash == replacement.hash || p.id == replacement.id);
	                }))
		throw std::runtime_error("Another installed pack already uses this identity; use Save as");
	*entry = replacement;
	atomic_write(path("packs", replacement.hash, ".zip"), archive);
	// Resolve the selected dependency closure before committing the index. Failed
	// verification or index replacement leaves the previous pack available.
	apply(selected);
	return replacement;
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
