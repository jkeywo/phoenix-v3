module;
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <set>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.menu;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.distribution;
import phoenix.content.packs;
import phoenix.content.verification;
namespace phoenix
{
namespace
{
const Json global_defaults = {{"tools_scale", 100},
                              {"volume", 50},
                              {"sfx_volume", 50},
                              {"siren_volume", 50},
                              {"master_volume", 50},
                              {"language", "en"},
                              {"pseudo", false},
                              {"reduced_motion", false},
                              {"dust_motes", true},
                              {"reference_grid", true},
                              {"station_displays", Json::object()},
                              {"screen_devices", Json::object()}};
Json global_preferences(const Json &data)
{
	auto result = global_defaults;
	for (auto &[key, value] : result.items())
		if (data.contains(key))
			value = data.at(key);
	return result;
}
} // namespace

MenuLibrary::MenuLibrary(std::string directory, std::string scope)
    : directory_(std::move(directory)), scope_(std::move(scope))
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
	auto data = parse_json(read_file(std::string(encoded.begin(), encoded.end()), 4 * 1024 * 1024), "native library");
	profiles_ = data.value("base_profiles", Json::object());
	global_profiles_ = data.value("global_profiles", Json::object());
	if (!scope_.empty())
	{
		const auto profile = scope_.substr(0, scope_.find('/'));
		if (global_profiles_.empty())
			global_profiles_["default"] = global_preferences(data);
		const auto global = global_profiles_.value(profile, global_defaults);
		for (const auto &[key, value] : global.items())
			data[key] = value;
		if (profiles_.empty())
			profiles_["default/phoenix-base"] = {{"player_name", data.value("player_name", std::string("<username>"))},
			                                     {"avatar", data.value("avatar", Bytes{})},
			                                     {"packs", data.at("packs")},
			                                     {"saves", data.at("saves")}};
		const auto scoped = profiles_.value(scope_, Json{{"player_name", "<username>"},
		                                                 {"avatar", Bytes{}},
		                                                 {"packs", Json::array()},
		                                                 {"saves", Json::array()}});
		for (const auto &[key, value] : scoped.items())
			data[key] = value;
	}
	if (data.at("schema") != 1 || data.at("packs").size() > 256 || data.at("saves").size() > 4096)
		throw std::runtime_error("Unsupported native library index");
	MenuPreferences prefs;
	prefs.volume = data.at("volume");
	prefs.sfx_volume = data.value("sfx_volume", 50);
	prefs.siren_volume = data.value("siren_volume", 50);
	if (prefs.siren_volume < 0 || prefs.siren_volume > 100)
		throw std::runtime_error("Invalid siren volume");
	prefs.master_volume = data.value("master_volume", 50);
	prefs.tools_scale = data.value("tools_scale", 100);
	const auto devices = data.value("screen_devices", Json::object());
	if (!devices.is_object() || devices.size() > 128)
		throw std::runtime_error("Invalid saved screen devices");
	for (const auto &[screen, value] : devices.items())
	{
		if (screen.empty() || screen.size() > 4096 || !value.is_object())
			throw std::runtime_error("Invalid saved screen device identity");
		ScreenDevices device;
		device.audio_output = value.value("audio_output", std::string("default"));
		device.gamepad_serial = value.value("gamepad_serial", std::string{});
		device.gamepad_path = value.value("gamepad_path", std::string{});
		device.gamepad_model = value.value("gamepad_model", std::string{});
		device.gamepad_name = value.value("gamepad_name", std::string{});
		device.audio_name = value.value("audio_name", std::string{});
		for (const auto *text : {&device.audio_output, &device.gamepad_serial, &device.gamepad_path,
		                         &device.gamepad_model, &device.gamepad_name, &device.audio_name})
			if (text->size() > 4096)
				throw std::runtime_error("Saved screen device field exceeds bounds");
		prefs.screen_devices[screen] = std::move(device);
	}
	const auto displays = data.value("station_displays", Json::object());
	if (!displays.is_object() || displays.size() > 128)
		throw std::runtime_error("Invalid saved station displays");
	prefs.station_displays = displays.get<decltype(prefs.station_displays)>();
	for (const auto &[ship, seats] : prefs.station_displays)
	{
		if (ship.empty() || ship.size() > 256 || seats.size() > 32)
			throw std::runtime_error("Invalid saved station display map");
		for (const auto &[seat, identity] : seats)
			if (seat.empty() || seat.size() > 256 || identity.empty() || identity.size() > 2048)
				throw std::runtime_error("Invalid saved station display identity");
	}
	if (prefs.tools_scale < 75 || prefs.tools_scale > 200 || prefs.tools_scale % 25 != 0)
		throw std::runtime_error("Invalid tools scale");
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
		LibraryPack pack{p.at("hash"), p.at("id"), p.at("enabled"), p.value("url", std::string{})};
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
	if (deferred_)
		return;
	if (!writable_)
		throw std::runtime_error(
		    "Native library index could not be loaded; preserve and repair library.json before saving changes");
	Json data{{"schema", 1},
	          {"station_displays", preferences.station_displays},
	          {"tools_scale", preferences.tools_scale},
	          {"volume", preferences.volume},
	          {"sfx_volume", preferences.sfx_volume},
	          {"siren_volume", preferences.siren_volume},
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
	data["screen_devices"] = Json::object();
	for (const auto &[screen, device] : preferences.screen_devices)
		data["screen_devices"][screen] = {
		    {"audio_output", device.audio_output}, {"gamepad_serial", device.gamepad_serial},
		    {"gamepad_path", device.gamepad_path}, {"gamepad_model", device.gamepad_model},
		    {"gamepad_name", device.gamepad_name}, {"audio_name", device.audio_name}};
	for (const auto &p : packs)
		data["packs"].push_back({{"hash", p.hash}, {"id", p.id}, {"enabled", p.enabled}, {"url", p.url}});
	for (const auto &s : saves)
		data["saves"].push_back({{"id", s.id}, {"name", s.name}, {"scenario", s.scenario}, {"saved_at", s.saved_at}});
	if (!scope_.empty())
	{
		const auto index = std::filesystem::u8path(directory_) / "library.json";
		if (std::filesystem::exists(index))
		{
			const auto encoded = index.u8string();
			const auto latest =
			    parse_json(read_file(std::string(encoded.begin(), encoded.end()), 4 * 1024 * 1024), "native library");
			if (latest.contains("base_profiles"))
				profiles_ = latest.at("base_profiles");
			global_profiles_ = latest.value("global_profiles", global_profiles_);
		}
		profiles_[scope_] = {{"player_name", preferences.player_name},
		                     {"avatar", preferences.avatar},
		                     {"packs", data["packs"]},
		                     {"saves", data["saves"]}};
		data["base_profiles"] = profiles_;
		global_profiles_[scope_.substr(0, scope_.find('/'))] = global_preferences(data);
		data["global_profiles"] = global_profiles_;
		if (global_profiles_.contains("default"))
			for (const auto &[key, value] : global_profiles_.at("default").items())
				data[key] = value;
		if (profiles_.contains("default/phoenix-base"))
			for (const auto &[key, value] : profiles_.at("default/phoenix-base").items())
				data[key] = value;
	}
	const auto encoded = (std::filesystem::u8path(directory_) / "library.json").u8string();
	atomic_write(std::string(encoded.begin(), encoded.end()), json_bytes(data));
}
void MenuLibrary::launch_metadata(const Json &menu)
{
	(void)menu_catalog({menu});
	if (menu.value("sidecar_version", 0) != 1 || menu.value("schema", 0) != 1 || !menu.contains("base") ||
	    menu.contains("parent") || !menu.value("dependencies", Json::array()).empty() ||
	    !valid_hex_token(menu.value("hash", std::string{})))
		throw std::runtime_error("Invalid root menu metadata");
	const std::string id = menu.at("id");
	if (id.empty() || id.size() > 128 ||
	    id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
		throw std::runtime_error("Invalid base identity");
	const auto &base = menu.at("base");
	base_ = {id, menu.at("hash"), base.value("render_mode", std::string("sprites")),
	         base.value("interface_style", std::string("current"))};
	if ((base_.render_mode != "sprites" && base_.render_mode != "models") || base_.interface_style != "current")
		throw std::runtime_error("Unsupported base presentation");
}
Json MenuLibrary::metadata(const LibraryPack &pack) const
{
	try
	{
		const auto menu = parse_json(read_file(path("packs", pack.hash, ".zip.menu.json"), 1024 * 1024), "pack menu");
		if (menu.at("hash") == pack.hash && menu.at("id") == pack.id && menu.value("schema", 0) == 1 &&
		    menu.value("sidecar_version", 0) == 1)
		{
			(void)menu_catalog({menu});
			return menu;
		}
	}
	catch (const std::exception &)
	{
	}
	std::vector<Pack> ordered;
	std::set<std::string> visiting, loaded;
	std::function<void(const LibraryPack &)> append = [&](const LibraryPack &entry)
	{
		if (loaded.contains(entry.id))
			return;
		if (!visiting.insert(entry.id).second)
			throw std::runtime_error("Pack dependency cycle");
		auto archive = read_pack(read_pack_archive(entry.hash));
		for (const auto &id : pack_dependencies(archive.manifest()))
		{
			const auto dependency = std::find_if(packs.begin(), packs.end(),
			                                     [&](const auto &p)
			                                     {
				                                     return p.id == id;
			                                     });
			if (dependency == packs.end())
				throw std::runtime_error("Missing metadata dependency: " + id);
			append(*dependency);
		}
		visiting.erase(entry.id);
		loaded.insert(entry.id);
		ordered.push_back(std::move(archive));
	};
	append(pack);
	const auto menu = pack_menu(ordered);
	atomic_write(path("packs", pack.hash, ".zip.menu.json"), json_bytes(menu));
	return menu;
}
void MenuLibrary::launch_base(const Bytes &archive)
{
	const auto root = read_pack(archive);
	const auto config = base_configuration(root);
	if (!base_.id.empty() && base_ != config)
		throw std::runtime_error("The launch base is fixed until restart");
	resolve_packs({root});
	base_ = config;
	atomic_write(path("packs", root.hash(), ".zip"), archive);
	atomic_write(path("packs", root.hash(), ".zip.menu.json"), json_bytes(pack_menu({root})));
	std::string published_url;
	for (const auto &p : packs)
		if (p.id == config.id)
			published_url = p.url;
	std::erase_if(packs,
	              [&](const auto &p)
	              {
		              return p.id == config.id || p.id == "test" || p.id == "phoenix-training";
	              });
	packs.insert(packs.begin(), {root.hash(), config.id, true, published_url});
	archives(packs);
	remember();
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
	const auto base = read_pack(base_archive);
	install_default(base_archive);
	const auto current = std::find_if(packs.begin(), packs.end(),
	                                  [&](const auto &pack)
	                                  {
		                                  return pack.id == base.manifest().at("id").get<std::string>();
	                                  });
	if (current != packs.end() && current->hash != base.hash())
		replace_pack(current->hash, base_archive);
	// User-authored mods remain independent. Bundled base edits are deliberately refreshed, without backup.
	install_default(addon_archive);
}

std::vector<LibraryPack> MenuLibrary::toggle_pack(const std::vector<LibraryPack> &selection,
                                                  const std::string &hash) const
{
	auto candidate = selection;
	std::vector<std::vector<std::string>> dependencies;
	for (const auto &p : candidate)
		dependencies.push_back(pack_dependencies(metadata(p)));
	const auto entry = std::find_if(candidate.begin(), candidate.end(),
	                                [&](const auto &p)
	                                {
		                                return p.hash == hash;
	                                });
	if (entry == candidate.end())
		throw std::runtime_error("Select an installed pack");
	const auto index = static_cast<std::size_t>(entry - candidate.begin());
	if (!base_.id.empty() && entry->id == base_.id)
		throw std::runtime_error("The launch base cannot be disabled");
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
	if (!base_.id.empty() && (pack.manifest().at("id") == base_.id || !pack.manifest().contains("parent")))
		throw std::runtime_error("Import a child of the launched base; the base cannot be replaced");
	if (!base_.id.empty())
	{
		std::set<std::string> seen;
		auto parent = pack.manifest().at("parent").get<std::string>();
		while (parent != base_.id)
		{
			if (!seen.insert(parent).second || parent == pack.manifest().at("id").get<std::string>())
				throw std::runtime_error("Pack parent cycle");
			const auto found = std::find_if(packs.begin(), packs.end(),
			                                [&](const auto &p)
			                                {
				                                return p.id == parent;
			                                });
			if (found == packs.end())
				throw std::runtime_error("Install the parent mod first: " + parent);
			const auto manifest = read_pack(read_pack_archive(found->hash)).manifest();
			if (!manifest.contains("parent"))
				throw std::runtime_error("The pack belongs to another base");
			parent = manifest.at("parent");
		}
	}
	const auto format = pack.manifest().value("format", std::string{});
	if (!format.empty() && format != "phoenix-mod-pack")
		throw std::runtime_error("Import gameplay packs here; use Saved Games for runtime saves");
	if (std::any_of(packs.begin(), packs.end(),
	                [&](const auto &p)
	                {
		                return p.hash == pack.hash();
	                }))
		return;
	if (std::any_of(packs.begin(), packs.end(),
	                [&](const auto &p)
	                {
		                return p.id == pack.manifest().at("id").get<std::string>();
	                }))
		throw std::runtime_error("A pack with this ID is already installed; replace or remove it first");
	if (packs.size() >= 256)
		throw std::runtime_error("Native pack library is full");
	atomic_write(path("packs", pack.hash(), ".zip"), archive);
	packs.push_back({pack.hash(), pack.manifest().at("id"), false});
	try
	{
		if (!base_.id.empty())
			archives(packs);
		(void)metadata(packs.back());
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
	if (!base_.id.empty() && (selection.empty() || selection.front().id != base_.id ||
	                          selection.front().hash != base_.hash || !selection.front().enabled))
		throw std::runtime_error("The launch base must remain enabled and first");
	std::vector<Pack> loaded;
	std::map<std::string, std::string> parents;
	std::set<std::string> identities;
	for (const auto &p : selection)
	{
		if (!p.enabled && std::filesystem::exists(std::filesystem::u8path(path("packs", p.hash, ".zip.menu.json"))))
		{
			const auto menu = metadata(p);
			const auto parent = menu.value("parent", std::string{});
			if (!base_.id.empty() && (!parents.contains(parent) || !identities.insert(p.id).second))
				throw std::runtime_error("Invalid disabled mod metadata");
			parents[p.id] = parent;
			continue;
		}
		auto bytes = read_file(path("packs", p.hash, ".zip"));
		auto pack = read_pack(bytes);
		if (pack.hash() != p.hash || pack.manifest().at("id").get<std::string>() != p.id ||
		    !identities.insert(p.id).second)
			throw std::runtime_error("Installed pack failed its identity check: " + p.id);
		if (!base_.id.empty())
		{
			if (p.id != base_.id)
			{
				if (!pack.manifest().contains("parent"))
					throw std::runtime_error("Child pack requires a parent: " + p.id);
				const auto parent = pack.manifest().at("parent").get<std::string>();
				if (!parents.contains(parent))
					throw std::runtime_error(
					    "Keep installed parents before their children; remove descendants first: " + p.id);
				parents[p.id] = parent;
			}
			else
				parents[p.id] = "";
		}
		if (p.enabled)
		{
			loaded.push_back(std::move(pack));
			result.push_back(std::move(bytes));
		}
	}
	if (loaded.size() > 32)
		throw std::runtime_error("Enable at most 32 ordered packs");
	if (!loaded.empty())
	{
		resolve_packs(loaded);
		std::vector<Pack> prefix;
		for (const auto &pack : loaded)
		{
			prefix.push_back(pack);
			const auto filename = path("packs", pack.hash(), ".zip.menu.json");
			if (!std::filesystem::exists(std::filesystem::u8path(filename)))
				atomic_write(filename, json_bytes(pack_menu(prefix)));
		}
	}
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
	if (!base_.id.empty() && hash == base_.hash)
		throw std::runtime_error("The launch base cannot be replaced; export and relaunch instead");
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
	// Content-addressed blobs may be referenced by another base profile.
}
ManagedSave MenuLibrary::store_save(const Bytes &archive, const std::string &name, const std::string &scenario)
{
	const auto pack = read_pack(archive, ArchivePurpose::Save);
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
	return read_file(path("saves", id, ".zip"), runtime_save_archive_limit);
}
} // namespace phoenix
