#pragma once
// Included in the native adapter's private namespace after its JournalFile.
// OS operations own locking and durability; this worker owns protocol and scheduling.
class NativeStore final : public PeerStore
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
		std::uint64_t generation = 0;
		try
		{
			const auto path = std::filesystem::absolute(std::filesystem::u8path(location));
			std::filesystem::create_directories(path.parent_path());
			JournalFile file;
			file.open(path);
			auto recovered = storage::replay(file.read(), false);
			auto state = std::move(recovered.state);
			file.recover(recovered.complete_bytes);
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
					file.close();
					replacement.commit();
					file.reopen();
					size = record.size();
				}
				else
				{
					file.append(record);
					size += record.size();
				}
				result({false, generation, nullptr, {}});
			}
		}
		catch (const std::exception &error)
		{
			result({generation == 0, generation, nullptr, error.what()});
		}
	}

public:
	explicit NativeStore(std::string location)
	    : worker_(
	          [this, location = std::move(location)]
	          {
		          run(location);
	          })
	{
	}
	~NativeStore() override
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
