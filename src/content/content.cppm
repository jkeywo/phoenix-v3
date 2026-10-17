module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.content;
export import phoenix.data;

export namespace phoenix
{
struct Catalog;
struct Pack
{
	Json manifest;
	std::map<std::string, Bytes> files;
	std::string hash;

private:
	Bytes archive_;
	friend Pack read_pack(std::span<const std::uint8_t> bytes);
	friend Catalog resolve_packs(const std::vector<Pack> &ordered);
};
struct Catalog
{
	std::map<std::string, Json> entities, maps, consoles, trees;
	std::map<std::string, Bytes> assets;
	std::vector<std::pair<std::string, std::string>> ordered_packs;
};
// Throws an actionable exception. ZIP is read into bounded memory, never
// extracted to caller-controlled paths on disk.
Pack read_pack(std::span<const std::uint8_t> bytes);
Bytes write_pack(const Pack &pack);
Catalog resolve_packs(const std::vector<Pack> &ordered);
void verify_catalog(const Catalog &catalog);
Bytes read_file(const std::string &path);
void write_file(const std::string &path, std::span<const std::uint8_t> bytes);
std::string sha256(std::span<const std::uint8_t> bytes);
Bytes json_bytes(const Json &json);
Json parse_json(std::span<const std::uint8_t> bytes, const std::string &source);
} // namespace phoenix

export namespace phoenix
{
struct TreeState
{
	std::string state;
	std::uint64_t elapsed = 0;
	bool entered = false;
};
// Each state emits bounded operating requests on entry, waits a declared tick
// duration and follows a named transition. Requests still require admission.
void verify_tree(const Json &definition, const std::string &source);
Json step_tree(const Json &definition, TreeState &state);
Json save_tree(const TreeState &state);
TreeState load_tree(const Json &definition, const Json &saved);
} // namespace phoenix

export namespace phoenix
{
class WorkshopDocument
{
public:
	WorkshopDocument();
	void reset();
	void load_pack(std::span<const std::uint8_t> archive);
	void load_dependency(std::span<const std::uint8_t> archive);
	void rename_pack(const std::string &id);
	Bytes export_pack() const;
	void load_map(std::span<const std::uint8_t> bytes);
	Bytes save_map() const;
	void new_map(const std::string &id);
	void select_map(std::size_t index);
	std::string create_entity(const std::string &parent = {});
	std::uint64_t place(const std::string &definition, std::int64_t x, std::int64_t y);
	void erase(std::uint64_t instance);
	void duplicate(std::uint64_t instance);
	std::string import_image(const std::string &filename, Bytes bytes);
	Catalog verify() const;
	Json &definitions()
	{
		return definitions_;
	}
	const Json &definitions() const
	{
		return definitions_;
	}
	Json &map()
	{
		return definitions_["maps"][map_index_];
	}
	const Json &map() const
	{
		return definitions_["maps"][map_index_];
	}
	Json &manifest()
	{
		return pack_.manifest;
	}
	const Pack &pack() const
	{
		return pack_;
	}
	const Catalog &libraries() const
	{
		return library_catalog_;
	}

private:
	Pack pack_;
	Json definitions_;
	std::size_t map_index_ = 0;
	std::vector<Pack> libraries_;
	Catalog library_catalog_;
};
} // namespace phoenix
