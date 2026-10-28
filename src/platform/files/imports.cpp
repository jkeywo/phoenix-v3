module;
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.files;

namespace phoenix
{
struct FileImportInbox::State
{
	ImportId pending = 0;
	FilePurpose purpose{};
	std::optional<ImportedFile> result;
	static inline ImportId next = 0;
	static auto &requests()
	{
		static std::map<ImportId, std::weak_ptr<State>> registry;
		return registry;
	}
};
FileImportInbox::FileImportInbox() : state_(std::make_shared<State>())
{
	// Construct the registry before any inbox owner can register its destructor.
	// Static UI owners must reset their inboxes before the registry is destroyed.
	(void)State::requests();
}
FileImportInbox::~FileImportInbox()
{
	reset();
}
ImportId FileImportInbox::begin(FilePurpose purpose)
{
	if (state_->pending)
		throw std::runtime_error("A file import is already pending for this view");
	if (State::next == std::numeric_limits<ImportId>::max())
		throw std::runtime_error("File request identifiers exhausted");
	state_->pending = ++State::next;
	state_->purpose = purpose;
	State::requests().emplace(state_->pending, state_);
	return state_->pending;
}
void FileImportInbox::reset()
{
	State::requests().erase(state_->pending);
	state_->pending = 0;
	state_->result.reset();
}
std::optional<ImportedFile> FileImportInbox::take()
{
	if (!state_->result)
		return {};
	auto result = std::move(state_->result);
	reset();
	return result;
}
void complete_import(ImportId id, std::string filename, Bytes bytes, std::string error, bool cancelled,
                     std::string destination)
{
	auto &requests = FileImportInbox::State::requests();
	auto found = requests.find(id);
	if (found == requests.end())
		return;
	auto state = found->second.lock();
	requests.erase(found);
	if (!state || state->pending != id)
		return;
	if (bytes.size() > file_import_limit(state->purpose))
		error =
		    state->purpose == FilePurpose::GameSave ? "File exceeds 128 MiB" : "File exceeds its content/import limit";
	const auto status = cancelled       ? ImportStatus::Cancelled
	                    : error.empty() ? ImportStatus::Success
	                                    : ImportStatus::Failed;
	if (status != ImportStatus::Success)
		bytes.clear();
	state->result = ImportedFile{
	    id, state->purpose, status, std::move(filename), std::move(bytes), std::move(error), std::move(destination)};
}
void open_file(ImportId id, FilePurpose purpose);
void request_file(FileImportInbox &inbox, FilePurpose purpose)
{
	const auto id = inbox.begin(purpose);
	try
	{
		open_file(id, purpose);
	}
	catch (const std::exception &error)
	{
		complete_import(id, {}, {}, error.what());
	}
}
void save_file(ImportId id, const std::string &filename, const Bytes &bytes, const std::string &destination,
               bool save_as);
void persist_library(ImportId id, const std::string &filename);
void request_library_save(FileImportInbox &inbox, const std::string &filename)
{
	const auto id = inbox.begin(FilePurpose::WorkshopPackSave);
	try
	{
		persist_library(id, filename);
	}
	catch (const std::exception &error)
	{
		complete_import(id, {}, {}, error.what());
	}
}
void request_save(FileImportInbox &inbox, FilePurpose purpose, const std::string &filename, const Bytes &bytes,
                  const std::string &destination, bool save_as)
{
	if (purpose != FilePurpose::WorkshopPackSave && purpose != FilePurpose::WorkshopMapSave)
		throw std::runtime_error("Unsupported save purpose");
	const auto id = inbox.begin(purpose);
	try
	{
		save_file(id, filename, bytes, destination, save_as);
	}
	catch (const std::exception &error)
	{
		complete_import(id, {}, {}, error.what());
	}
}
} // namespace phoenix
