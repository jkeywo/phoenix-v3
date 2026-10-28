module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
module phoenix.data;
namespace phoenix
{
struct StagedFileReplacement::State
{
	std::filesystem::path destination, temporary;
	std::string context;
	bool owned = false, flushed = false;
	~State()
	{
		if (owned)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
		}
	}
	[[noreturn]] void fail(const char *operation) const
	{
		throw std::runtime_error(context + ": " + operation);
	}
};
StagedFileReplacement::StagedFileReplacement(const std::string &destination, std::span<const std::uint8_t> bytes,
                                             FileWriteMode mode, const std::string &context)
    : state_(std::make_unique<State>())
{
	auto &state = *state_;
	state.destination = std::filesystem::u8path(destination);
	state.temporary = state.destination.parent_path() / (random_token() + ".tmp");
	state.context = context;
	state.flushed = mode == FileWriteMode::Flushed;
#ifdef _WIN32
	if (bytes.size() > std::numeric_limits<DWORD>::max())
		state.fail("replacement exceeds write limit");
	const auto flags = FILE_ATTRIBUTE_NORMAL | (mode == FileWriteMode::Flushed ? FILE_FLAG_WRITE_THROUGH : 0);
	const auto file = CreateFileW(state.temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, flags, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		state.fail("cannot create temporary file");
	state.owned = true;
	const std::unique_ptr<void, decltype(&CloseHandle)> handle(file, &CloseHandle);
	DWORD written = 0;
	if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size())
		state.fail("temporary file write failed");
	if (mode == FileWriteMode::Flushed && !FlushFileBuffers(file))
		state.fail("temporary file flush failed");
#elif !defined(__EMSCRIPTEN__)
	const int file = ::open(state.temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (file < 0)
		state.fail("cannot create temporary file");
	state.owned = true;
	try
	{
		std::size_t offset = 0;
		while (offset < bytes.size())
		{
			const auto count = ::write(file, bytes.data() + offset, bytes.size() - offset);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				state.fail("temporary file write failed");
			offset += static_cast<std::size_t>(count);
		}
		if (state.flushed && ::fsync(file) != 0)
			state.fail("temporary file flush failed");
	}
	catch (...)
	{
		::close(file);
		throw;
	}
	if (::close(file) != 0)
		state.fail("temporary file close failed");
#else
	if (mode == FileWriteMode::Flushed)
		state.fail("flushed replacement is unavailable on this platform");
	// Browser filesystem writes are persisted by their existing caller after rename.
	while (std::filesystem::exists(state.temporary))
		state.temporary = state.destination.parent_path() / (random_token() + ".tmp");
	state.owned = true;
	const auto encoded = state.temporary.u8string();
	write_file(std::string(encoded.begin(), encoded.end()), bytes);
#endif
}
StagedFileReplacement::~StagedFileReplacement() = default;
StagedFileReplacement::StagedFileReplacement(StagedFileReplacement &&) noexcept = default;
StagedFileReplacement &StagedFileReplacement::operator=(StagedFileReplacement &&) noexcept = default;
void StagedFileReplacement::commit()
{
	if (!state_ || !state_->owned)
		throw std::logic_error("No staged file replacement to commit");
	auto &state = *state_;
#ifdef _WIN32
	if (!MoveFileExW(state.temporary.c_str(), state.destination.c_str(),
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		state.fail("atomic replacement failed");
#else
	std::filesystem::rename(state.temporary, state.destination);
#ifndef __EMSCRIPTEN__
	if (state.flushed)
	{
		const auto parent =
		    state.destination.parent_path().empty() ? std::filesystem::path(".") : state.destination.parent_path();
		const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (directory < 0)
			state.fail("cannot open replacement directory");
		const auto status = ::fsync(directory);
		::close(directory);
		if (status != 0)
			state.fail("replacement directory flush failed");
	}
#endif
#endif
	state.owned = false;
}
} // namespace phoenix
