// Included inside World::Impl. Detach a phase's due jobs before execution so
// callbacks may enqueue later work without invalidating iteration or replaying it.
std::size_t draining_shield_reactions = 0, draining_payloads = 0;

static void record_effect_result(std::map<std::uint64_t, std::string> &results, std::uint64_t id, std::string status)
{
	results[id] = std::move(status);
	while (results.size() > 1024)
		results.erase(results.begin());
}
template <typename Job, typename Execute>
static void drain_effect_queue(std::vector<Job> &pending, std::map<std::uint64_t, std::string> &results,
                               std::size_t &draining, std::int64_t tick, Execute execute)
{
	std::vector<Job> due;
	std::size_t retained = 0;
	for (std::size_t read = 0; read < pending.size(); ++read)
	{
		if (pending[read].due <= tick)
			due.push_back(std::move(pending[read]));
		else
		{
			if (retained != read)
				pending[retained] = std::move(pending[read]);
			++retained;
		}
	}
	pending.resize(retained);
	std::sort(due.begin(), due.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	// Detached jobs still occupy queue capacity until their turn, matching
	// per-job removal without repeatedly shifting the pending vector.
	draining = due.size();
	try
	{
		for (const auto &job : due)
		{
			--draining;
			record_effect_result(results, job.id, "failed: required target or grant unavailable");
			const auto failure = execute(job);
			record_effect_result(results, job.id, failure.empty() ? "committed" : "failed: " + failure);
		}
	}
	catch (...)
	{
		for (auto i = due.size() - draining; i < due.size(); ++i)
			pending.push_back(std::move(due[i]));
		draining = 0;
		throw;
	}
}
