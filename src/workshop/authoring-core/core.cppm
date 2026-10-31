module;
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

export module phoenix.workshop.authoring_core;
export import phoenix.content.documents;

export namespace phoenix
{
// Immutable descriptions drive both the standard GUI controls and CLI discovery.
const Json &workshop_fields();
const Json &workshop_field(const std::string &id);
const Json &workshop_records();
const Json &workshop_record(const std::string &id);
Json workshop_record_value(const std::string &, const Json &context);
Json workshop_append_record(Json &rows, Json value, bool generate_identity);
void workshop_set_field(Json &object, const Json &field, const Json &value);
Json workshop_colour_channels(const std::string &);
std::string workshop_colour_text(const Json &);
Json workshop_control(const std::string &, const Json &);
Json workshop_control(const std::string &, const Json &, const Json &context);
Json workshop_set_control(Json &, const std::string &, const Json &);
Json workshop_set_control(Json &, const std::string &, const Json &, const Json &context);
Json workshop_custom_actions();
Json workshop_custom_action(Json &, const std::string &);
Json workshop_custom_action(Json &, const std::string &, const Json &arguments);
void workshop_sprite_apply(WorkshopDocument &, Json &, const Json &sidecar, const Json &images, bool engine_effect);
Json workshop_asset_choices(const WorkshopDocument &, bool skybox);
Json workshop_layout_palette(WorkshopDocument &, const Json &, bool header, const std::string &context);
Json workshop_instance_fields(WorkshopDocument &, const std::set<std::uint64_t> &);
void workshop_instance_field(WorkshopDocument &, const std::set<std::uint64_t> &, const std::string &path,
                             const Json &value);
Json workshop_operations();
Json workshop_definition_kinds();
Json workshop_operation(const std::string &id);
void workshop_validate_arguments(const std::string &operation, const Json &arguments);
Json workshop_definition_default(const std::string &kind, const std::string &context = "controller");
Json workshop_prepare_vocabulary(const Json &);
Json workshop_preview_tree(const WorkshopDocument &, Json source);
Json workshop_tree_state(const std::string &id);
Json workshop_tree_localize(WorkshopDocument &, const Json &binding, const std::string &context, bool copy);
Json workshop_tree_promote(WorkshopDocument &, Json &binding, const std::string &id, bool policy);
Json workshop_tree_add_state(Json &, const std::string &id = {});
std::string workshop_tree_removal_error(const Json &, const std::string &id);
void workshop_tree_remove_state(Json &, const std::string &id);
Json workshop_tree_task(const std::string &kind);
Json workshop_action_arguments(const Json &signature);
Json workshop_layout_action(WorkshopDocument &, Json &, const std::string &action, const Json &arguments);
Json workshop_ship_graph(WorkshopDocument &, const std::string &entity, const std::string &action,
                         const Json &arguments);
void workshop_set_parent(WorkshopDocument &, const std::string &parent);
const Json &workshop_seeds();
const Json &workshop_seed(const std::string &id);
Json *workshop_layout_node(Json &, const std::string &identity, const std::string &prefix = {});
bool workshop_layout_remove(Json &, const std::string &identity, const std::string &prefix = {});
Json workshop_layout_drop(Json &root, const Json &arguments, const Catalog &);
void workshop_tree_rename(Json &, const std::string &from, const std::string &to);
Json workshop_ship_add(Json &authored, const Json &arguments);
Json workshop_tree_vocabulary(WorkshopDocument &, const Json &tree);
// Semantic operations work on a disposable candidate; they never save implicitly.
Json workshop_apply(WorkshopDocument &, const std::string &operation, const Json &arguments);
struct WorkshopDiagnostic
{
	std::string code, operation, target, message;
};
class WorkshopAuthoring
{
public:
	WorkshopAuthoring();
	const WorkshopDocument &document() const;
	Json request(const Json &request);
	// GUI buffers are disposable. Only this transaction publishes their changes.
	void commit(const WorkshopDocument &candidate, bool checkpoint = true);
	void checkpoint();
	void reset(const WorkshopDocument &);
	bool undo();
	bool redo();
	bool can_undo() const;
	bool can_redo() const;

private:
	WorkshopDocument document_;
	WorkshopHistory history_;
	std::uint64_t revision_ = 1;
	Json execute(const std::string &, const Json &);
};
// Native filesystem adapter: source drafts are not gameplay-verified packages.
PackDraft workshop_read_source(const std::string &path);
void workshop_write_files(const std::string &root, const std::map<std::string, Bytes> &);
void workshop_save_source(const WorkshopDocument &, const std::string &path);
void workshop_export(const WorkshopDocument &, const std::string &path, bool scenario_only = false);
Bytes workshop_source_archive(const WorkshopDocument &);
void workshop_export_map(const WorkshopDocument &, const std::string &path);
void workshop_export_source(const WorkshopDocument &, const std::string &path);
} // namespace phoenix

namespace phoenix
{
void workshop_publish(Json &, const Json &);
void workshop_controller_arguments(Json &);
} // namespace phoenix
