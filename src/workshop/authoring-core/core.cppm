module;
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
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
Json workshop_append_record(Json &rows, Json value, bool generate_identity);
void workshop_set_field(Json &object, const Json &field, const Json &value);
Json workshop_operations();
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
} // namespace phoenix
