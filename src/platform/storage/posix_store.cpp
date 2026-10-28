module;
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
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

module phoenix.platform.storage;

namespace phoenix
{
namespace
{
class PosixJournalFile final : public storage::JournalFile
{
	int file_ = -1, ownership_ = -1;
	std::filesystem::path path_;

public:
	PosixJournalFile() = default;
	PosixJournalFile(const PosixJournalFile &) = delete;
	PosixJournalFile &operator=(const PosixJournalFile &) = delete;
	~PosixJournalFile() override
	{
		close();
		if (ownership_ >= 0)
			::close(ownership_);
	}
	void open(const std::filesystem::path &path)
	{
		path_ = path;
		auto lock_path = path_;
		lock_path += ".writer";
		ownership_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (ownership_ < 0 || ::flock(ownership_, LOCK_EX | LOCK_NB) != 0)
			throw std::runtime_error("Peer journal already has a writer or cannot be locked");
		file_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (file_ < 0)
			throw std::runtime_error("Cannot open peer journal");
	}
	std::string read() override
	{
		struct stat info{};
		if (::fstat(file_, &info) != 0 || info.st_size < 0 || info.st_size > storage::max_journal)
			throw std::runtime_error("Peer journal length is invalid or exceeds limit");
		std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
		std::size_t offset = 0;
		while (offset < bytes.size())
		{
			const auto count = ::read(file_, bytes.data() + offset, bytes.size() - offset);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				throw std::runtime_error("Cannot read peer journal");
			offset += static_cast<std::size_t>(count);
		}
		return bytes;
	}
	void recover(std::size_t complete_bytes) override
	{
		if (::ftruncate(file_, complete_bytes) != 0 || ::lseek(file_, complete_bytes, SEEK_SET) < 0 ||
		    ::fsync(file_) != 0)
			throw std::runtime_error("Cannot recover the complete peer journal prefix");
		// Acknowledge creation only once the directory entry is durable.
		const int directory = ::open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (directory < 0)
			throw std::runtime_error("Cannot open peer journal directory");
		const auto flushed = ::fsync(directory);
		::close(directory);
		if (flushed != 0)
			throw std::runtime_error("Cannot flush peer journal directory");
	}
	void close() override
	{
		if (file_ >= 0)
			::close(file_);
		file_ = -1;
	}
	void reopen() override
	{
		file_ = ::open(path_.c_str(), O_RDWR | O_CLOEXEC);
		if (file_ < 0 || ::lseek(file_, 0, SEEK_END) < 0)
			throw std::runtime_error("Cannot reopen checkpoint journal");
	}
	void append(const std::string &record) override
	{
		std::size_t written = 0;
		while (written < record.size())
		{
			const auto count = ::write(file_, record.data() + written, record.size() - written);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				throw std::runtime_error("Peer journal write failed");
			written += static_cast<std::size_t>(count);
		}
		if (::fsync(file_) != 0)
			throw std::runtime_error("Peer journal flush failed");
	}
};
} // namespace
std::unique_ptr<storage::JournalFile> storage::open_journal_file(const std::filesystem::path &path)
{
	auto file = std::make_unique<PosixJournalFile>();
	file->open(path);
	return file;
}
} // namespace phoenix
