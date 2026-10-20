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

module phoenix.persistence;
#include "../replication/store_internal.hpp"

namespace phoenix
{
namespace
{
class PosixStore final : public PeerStore
{
	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<PeerStoreResult> results_;
	std::optional<DurableWrite> job_;
	bool stopping_ = false, busy_ = true, ready_ = false, failed_ = false;
	std::thread worker_;
	void result(PeerStoreResult value)
	{
		std::lock_guard lock(mutex_);
		if (!value.error.empty())
			failed_ = true;
		results_.push_back(std::move(value));
	}
	void run(std::string location)
	{
		int file = -1;
		int ownership = -1;
		std::uint64_t generation = 0;
		try
		{
			auto path = std::filesystem::absolute(std::filesystem::u8path(location));
			std::filesystem::create_directories(path.parent_path());
			auto lock_path = path;
			lock_path += ".writer";
			ownership = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
			if (ownership < 0 || ::flock(ownership, LOCK_EX | LOCK_NB) != 0)
				throw std::runtime_error("Peer journal already has a writer or cannot be locked");
			file = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
			if (file < 0)
				throw std::runtime_error("Cannot open peer journal");
			struct stat info{};
			if (::fstat(file, &info) != 0 || info.st_size < 0 || info.st_size > storage::max_journal)
				throw std::runtime_error("Peer journal length is invalid or exceeds limit");
			std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
			std::size_t offset = 0;
			while (offset < bytes.size())
			{
				const auto count = ::read(file, bytes.data() + offset, bytes.size() - offset);
				if (count < 0 && errno == EINTR)
					continue;
				if (count <= 0)
					throw std::runtime_error("Cannot read peer journal");
				offset += static_cast<std::size_t>(count);
			}
			auto recovered = storage::replay(bytes, false);
			auto state = std::move(recovered.state);
			if (::ftruncate(file, recovered.complete_bytes) != 0 ||
			    ::lseek(file, recovered.complete_bytes, SEEK_SET) < 0 || ::fsync(file) != 0)
				throw std::runtime_error("Cannot recover the complete peer journal prefix");
			// Acknowledge creation only once the directory entry is durable.
			const int directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
			if (directory < 0)
				throw std::runtime_error("Cannot open peer journal directory");
			const auto flushed = ::fsync(directory);
			::close(directory);
			if (flushed != 0)
				throw std::runtime_error("Cannot flush peer journal directory");
			result({true, 0, state, {}});
			auto size = recovered.complete_bytes;
			for (;;)
			{
				DurableWrite job;
				{
					std::unique_lock lock(mutex_);
					wake_.wait(lock,
					           [&]
					           {
						           return stopping_ || job_.has_value();
					           });
					if (!job_)
						break;
					job = std::move(*job_);
					job_.reset();
				}
				generation = job.generation;
				auto record = storage::encode(job.transaction);
				const bool replace = job.transaction.contains("snapshot");
				if (record.size() > storage::max_journal - (replace ? 0 : size))
					throw std::runtime_error("Peer journal full; checkpoint required before more acknowledgements");
				apply_peer_write(state, job.transaction);
				if (replace)
				{
					const auto encoded_path = path.u8string();
					StagedFileReplacement replacement(
					    std::string(encoded_path.begin(), encoded_path.end()),
					    {reinterpret_cast<const std::uint8_t *>(record.data()), record.size()}, FileWriteMode::Flushed,
					    "Checkpoint journal");
					::close(file);
					file = -1;
					replacement.commit();
					file = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
					if (file < 0 || ::lseek(file, 0, SEEK_END) < 0)
						throw std::runtime_error("Cannot reopen checkpoint journal");
					size = record.size();
					result({false, generation, nullptr, {}});
					continue;
				}
				std::size_t written = 0;
				while (written < record.size())
				{
					const auto count = ::write(file, record.data() + written, record.size() - written);
					if (count < 0 && errno == EINTR)
						continue;
					if (count <= 0)
						throw std::runtime_error("Peer journal write failed");
					written += static_cast<std::size_t>(count);
				}
				if (::fsync(file) != 0)
					throw std::runtime_error("Peer journal flush failed");
				size += record.size();
				result({false, generation, nullptr, {}});
			}
		}
		catch (const std::exception &error)
		{
			result({generation == 0, generation, nullptr, error.what()});
		}
		if (file >= 0)
			::close(file);
		if (ownership >= 0)
			::close(ownership);
	}

public:
	explicit PosixStore(std::string location)
	    : worker_(
	          [this, location = std::move(location)]
	          {
		          run(location);
	          })
	{
	}
	~PosixStore() override
	{
		{
			std::lock_guard lock(mutex_);
			stopping_ = true;
		}
		wake_.notify_one();
		worker_.join();
	}
	bool write(const DurableWrite &job) override
	{
		std::lock_guard lock(mutex_);
		if (!ready_ || failed_ || busy_ || stopping_)
			return false;
		job_ = job;
		busy_ = true;
		wake_.notify_one();
		return true;
	}
	std::optional<PeerStoreResult> poll() override
	{
		std::lock_guard lock(mutex_);
		if (results_.empty())
			return {};
		auto value = std::move(results_.front());
		results_.pop_front();
		busy_ = false;
		if (value.opened && value.error.empty())
			ready_ = true;
		return value;
	}
};
} // namespace
std::unique_ptr<PeerStore> open_peer_store(const std::string &location)
{
	return std::make_unique<PosixStore>(location);
}
} // namespace phoenix
