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

export module phoenix.content.documents;
export import phoenix.data;
export import phoenix.state_tree;
export import phoenix.content.catalog;
export import phoenix.content.packs;

export namespace phoenix
{
enum class WorkshopDefinitionKind
{
	Entity,
	Map,
	Console,
	Tree,
	Equipment,
	Assembly,
	Message,
	Effect,
	Resource,
	Property,
	Recipe,
	Layout,
	UiGroup,
	Theme,
	ControlWidget
};
class WorkshopDocument
{
	friend class WorkshopHistory;

public:
	WorkshopDocument();
	void reset();
	void load_pack(std::span<const std::uint8_t> archive);
	void load_dependency(std::span<const std::uint8_t> archive);
	void rename_pack(const std::string &id);
	Bytes export_pack(PackEncoding encoding = PackEncoding::Zip, bool scenario_only = false) const;
	std::map<std::string, Bytes> export_companions(const std::string &filename, PackEncoding encoding,
	                                               bool scenario_only) const;
	void load_map(std::span<const std::uint8_t> bytes);
	Bytes save_map() const;
	void new_map(const std::string &id);
	void select_map(std::size_t index);
	std::string create_entity(const std::string &parent = {});
	void rename_entity(const std::string &from, const std::string &to);
	void rename_console(const std::string &from, const std::string &to);
	void rename_ui_definition(WorkshopDefinitionKind, const std::string &from, const std::string &to);
	std::uint64_t place(const std::string &definition, std::int64_t x, std::int64_t y);
	void erase(std::uint64_t instance);
	void duplicate(std::uint64_t instance);
	std::string import_image(const std::string &filename, Bytes bytes);
	std::string import_font(const std::string &filename, Bytes bytes);
	std::string import_model(const std::string &filename, Bytes bytes);
	Catalog verify() const;
	// Resolves reusable equipment and controller namespaces without requiring finished ownership/presets.
	Catalog preview_equipment() const;
	std::shared_ptr<const Catalog> preview_ui() const;
	Json preview_tree(const Json &tree) const;
	Json preview_tree(const std::string &id) const;
	std::vector<std::string> definition_choices(WorkshopDefinitionKind kind) const;
	Json preview_definition(WorkshopDefinitionKind kind, const std::string &id) const;
	Json preview_knowledge() const;
	state_tree::Registry preview_controller_registry(const Json &tree) const;
	Json &definitions();
	const Json &definitions() const;
	Json &map();
	const Json &map() const;
	Json &manifest();
	const PackDraft &pack() const;
	const Catalog &libraries() const;
	std::vector<Bytes> dependency_archives() const;

private:
	mutable std::shared_ptr<const Catalog> ui_preview_;
	mutable Json ui_preview_source_, ui_preview_manifest_;
	PackDraft pack_;
	Json definitions_;
	std::size_t map_index_ = 0;
	std::vector<Pack> libraries_;
	Catalog library_catalog_;
};
// Complete draft history, including imported assets and dependency closure. Selection is not an edit.
class WorkshopHistory
{
public:
	void reset(const WorkshopDocument &);
	void checkpoint(const WorkshopDocument &);
	bool can_undo() const;
	bool can_redo() const;
	bool undo(WorkshopDocument &);
	bool redo(WorkshopDocument &);

private:
	struct Snapshot
	{
		Json definitions, manifest;
		std::size_t map_index, bytes, resource_bytes;
		std::shared_ptr<const WorkshopDocument> resources;
	};
	std::vector<Snapshot> past_, future_;
	static bool same_resources(const WorkshopDocument &, const WorkshopDocument &);
	static void restore(WorkshopDocument &, const Snapshot &);
	Snapshot snapshot(const WorkshopDocument &) const;
	static void trim(std::vector<Snapshot> &);
};
} // namespace phoenix
