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
	WorkshopSprite,
	WorkshopPackSave,
	WorkshopMapSave,
	PlayerAvatar,
	WorkshopModel,
	WorkshopFont
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
	// Native absolute path or an opaque browser File System Access handle token.
	// Empty when the platform cannot retain write access to the selected file.
	std::string destination;
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
	friend void complete_import(ImportId, std::string, Bytes, std::string, bool, std::string);
};
// Platform callback ingress. Unknown, stale and duplicate IDs have no effect.
void complete_import(ImportId id, std::string filename, Bytes bytes, std::string error = {}, bool cancelled = false,
                     std::string destination = {});
void request_file(FileImportInbox &inbox, FilePurpose purpose);
// Uses the same owned completion/cancellation lifecycle as imports. Save As asks
// for a new destination; ordinary Save reuses the supplied destination.
void request_save(FileImportInbox &inbox, FilePurpose purpose, const std::string &filename, const Bytes &bytes,
                  const std::string &destination = {}, bool save_as = false);
// A managed pack is already written through MenuLibrary. Browser completion
// waits for the launcher's durable IndexedDB transaction.
void request_library_save(FileImportInbox &inbox, const std::string &filename);
void export_file(const std::string &filename, const Bytes &bytes);
void enable_workshop_file_shortcuts();
void copy_text(const std::string &text);
// An empty error is a completed platform copy, not merely an enqueued request.
std::optional<std::string> take_copy_result();
} // namespace phoenix
