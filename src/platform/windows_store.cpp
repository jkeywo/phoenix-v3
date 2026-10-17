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

module phoenix.persistence;
#include "../replication/store_internal.hpp"

namespace phoenix
{
namespace
{
class WindowsStore final : public PeerStore
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
		HANDLE file = INVALID_HANDLE_VALUE;
		std::uint64_t generation = 0;
		try
		{
			auto path = std::filesystem::absolute(std::filesystem::u8path(location));
			std::filesystem::create_directories(path.parent_path());
			file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
			                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				throw std::runtime_error("Cannot open/lock peer journal: " + std::to_string(GetLastError()));
			LARGE_INTEGER length{};
			if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || length.QuadPart > storage::max_journal)
				throw std::runtime_error("Peer journal length is invalid or exceeds limit");
			std::string bytes(static_cast<std::size_t>(length.QuadPart), '\0');
			DWORD count = 0;
			if (!bytes.empty() && (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
			                       count != bytes.size()))
				throw std::runtime_error("Cannot read peer journal");
			auto recovered = storage::replay(bytes, false);
			auto state = std::move(recovered.state);
			LARGE_INTEGER end{};
			end.QuadPart = static_cast<LONGLONG>(recovered.complete_bytes);
			if (!SetFilePointerEx(file, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file) || !FlushFileBuffers(file))
				throw std::runtime_error("Cannot recover the complete peer journal prefix");
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
				if (record.size() > storage::max_journal - size)
					throw std::runtime_error("Peer journal full; checkpoint required before more acknowledgements");
				apply_peer_write(state, job.transaction);
				DWORD written = 0;
				if (!WriteFile(file, record.data(), static_cast<DWORD>(record.size()), &written, nullptr) ||
				    written != record.size() || !FlushFileBuffers(file))
					throw std::runtime_error("Peer journal write/flush failed: " + std::to_string(GetLastError()));
				size += record.size();
				result({false, generation, nullptr, {}});
			}
		}
		catch (const std::exception &error)
		{
			result({generation == 0, generation, nullptr, error.what()});
		}
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
	}

public:
	explicit WindowsStore(std::string location)
	    : worker_(
	          [this, location = std::move(location)]
	          {
		          run(location);
	          })
	{
	}
	~WindowsStore() override
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
	return std::make_unique<WindowsStore>(location);
}
} // namespace phoenix
