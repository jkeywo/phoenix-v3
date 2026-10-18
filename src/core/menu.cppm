module;
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.menu;
export import phoenix.content;

export namespace phoenix
{
// A launcher's working directory is neither stable nor necessarily writable.
std::string native_data_directory();
struct MenuPreferences
{
	int volume = 50;
	std::string language = "en";
	bool pseudo = false, reduced_motion = false;
	bool dust_motes = true, reference_grid = true;
};
struct LibraryPack
{
	std::string hash, id;
	bool enabled = true;
};
struct ManagedSave
{
	std::string id, name, scenario, saved_at;
};
// Native user data is separate from transport credentials and consensus journals.
class MenuLibrary
{
public:
	explicit MenuLibrary(std::string directory);
	MenuPreferences preferences;
	std::vector<LibraryPack> packs;
	std::vector<ManagedSave> saves;
	void load();
	void remember();
	void install(const Bytes &archive);
	void install_default(const Bytes &archive);
	Bytes read_pack_archive(const std::string &hash) const;
	LibraryPack replace_pack(const std::string &hash, const Bytes &archive);
	std::vector<Bytes> archives(const std::vector<LibraryPack> &selection) const;
	void apply(const std::vector<LibraryPack> &selection);
	ManagedSave store_save(const Bytes &archive, const std::string &name, const std::string &scenario);
	Bytes read_save(const std::string &id) const;

private:
	std::string directory_;
	bool writable_ = true;
	std::string path(const std::string &folder, const std::string &id, const std::string &extension) const;
	void atomic_write(const std::string &path, const Bytes &bytes) const;
};
} // namespace phoenix
