module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

module phoenix.persistence;
#include "../replication/store_internal.hpp"

namespace phoenix
{
namespace
{
class JournalFile
{
	HANDLE file_ = INVALID_HANDLE_VALUE, ownership_ = INVALID_HANDLE_VALUE;
	std::filesystem::path path_;

public:
	JournalFile() = default;
	JournalFile(const JournalFile &) = delete;
	JournalFile &operator=(const JournalFile &) = delete;
	~JournalFile()
	{
		close();
		if (ownership_ != INVALID_HANDLE_VALUE)
			CloseHandle(ownership_);
	}
	void open(const std::filesystem::path &path)
	{
		path_ = path;
		auto lock_path = path_;
		lock_path += L".writer";
		ownership_ = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
		                         FILE_ATTRIBUTE_NORMAL, nullptr);
		if (ownership_ == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Peer journal already has a writer");
		file_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
		                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		if (file_ == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Cannot open/lock peer journal: " + std::to_string(GetLastError()));
	}
	std::string read()
	{
		LARGE_INTEGER length{};
		if (!GetFileSizeEx(file_, &length) || length.QuadPart < 0 || length.QuadPart > storage::max_journal)
			throw std::runtime_error("Peer journal length is invalid or exceeds limit");
		std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0');
		DWORD count = 0;
		if (!bytes.empty() && (!ReadFile(file_, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
		                       count != bytes.size()))
			throw std::runtime_error("Cannot read peer journal");
		return bytes;
	}
	void recover(std::size_t complete_bytes)
	{
		LARGE_INTEGER end{};
		end.QuadPart = static_cast<LONGLONG>(complete_bytes);
		if (!SetFilePointerEx(file_, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file_) || !FlushFileBuffers(file_))
			throw std::runtime_error("Cannot recover the complete peer journal prefix");
	}
	void close()
	{
		if (file_ != INVALID_HANDLE_VALUE)
			CloseHandle(file_);
		file_ = INVALID_HANDLE_VALUE;
	}
	void reopen()
	{
		file_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
		                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		LARGE_INTEGER end{};
		if (file_ == INVALID_HANDLE_VALUE || !SetFilePointerEx(file_, end, nullptr, FILE_END))
			throw std::runtime_error("Cannot reopen checkpoint journal");
	}
	void append(const std::string &record)
	{
		DWORD written = 0;
		if (!WriteFile(file_, record.data(), static_cast<DWORD>(record.size()), &written, nullptr) ||
		    written != record.size() || !FlushFileBuffers(file_))
			throw std::runtime_error("Peer journal write/flush failed: " + std::to_string(GetLastError()));
	}
};
#include "native_store.hpp"
} // namespace
std::unique_ptr<PeerStore> open_peer_store(const std::string &location)
{
	return std::make_unique<NativeStore>(location);
}
} // namespace phoenix
