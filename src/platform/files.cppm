module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.files;
export import phoenix.data;

export namespace phoenix
{
using ImportId = std::uint32_t;
enum class FilePurpose
{
	GamePack,
	GameSave,
	WorkshopPack,
	WorkshopLibrary,
	WorkshopMap,
	WorkshopImage,
	WorkshopSprite
};
enum class ImportStatus
{
	Success,
	Cancelled,
	Failed
};
struct ImportedFile
{
	ImportId id;
	FilePurpose purpose;
	ImportStatus status;
	std::string filename;
	Bytes bytes;
	std::string error;
};
// One pending operation per consumer, retained until that consumer takes it.
// Reset/destruction invalidates pending callbacks, including completed results.
class FileImportInbox
{
public:
	FileImportInbox();
	~FileImportInbox();
	FileImportInbox(const FileImportInbox &) = delete;
	FileImportInbox &operator=(const FileImportInbox &) = delete;
	ImportId begin(FilePurpose purpose);
	std::optional<ImportedFile> take();
	void reset();

private:
	struct State;
	std::shared_ptr<State> state_;
	friend void complete_import(ImportId, std::string, Bytes, std::string, bool);
};
// Platform callback ingress. Unknown, stale and duplicate IDs have no effect.
void complete_import(ImportId id, std::string filename, Bytes bytes, std::string error = {}, bool cancelled = false);
void request_file(FileImportInbox &inbox, FilePurpose purpose);
void export_file(const std::string &filename, const Bytes &bytes);
void copy_text(const std::string &text);
// An empty error is a completed platform copy, not merely an enqueued request.
std::optional<std::string> take_copy_result();
} // namespace phoenix
