// Shared bounded diagnostic receipts for authored rule execution.
static void record_effect_result(std::map<std::uint64_t, std::string> &results, std::uint64_t id, std::string status)
{
	results[id] = std::move(status);
	while (results.size() > 1024)
		results.erase(results.begin());
}
