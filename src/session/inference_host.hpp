#pragma once
// Host-local scheduling. Every state change goes through the supplied commit path.
namespace phoenix
{
struct InferenceHost
{
	std::shared_ptr<InferenceConnection> connection;
	std::size_t cursor = 0;
	std::chrono::steady_clock::time_point coalesce_until{};
	std::map<std::string, std::string> basis;
	std::map<std::string, std::chrono::steady_clock::time_point> sent;
	void reset()
	{
		sent.clear();
		basis.clear();
		cursor = 0;
		coalesce_until = {};
		if (connection)
			connection->reset_requests();
	}

	template <class Commit> void poll(World &world, EntityId ship, Commit commit)
	{
		if (connection)
		{
			connection->poll();
			for (auto result : connection->completed())
				commit(Json{{"kind", "ai-decision"}, {"decision", std::move(result)}});
			if (connection->busy())
				return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now < coalesce_until)
			return;
		const auto rows = world.controller_ai(ship);
		Json batch = Json::array();
		const auto first = cursor;
		auto next_cursor = cursor;
		std::vector<std::pair<std::string, std::string>> selected;
		const auto interval =
		    connection ? connection->status().value("decision_interval_ms", std::uint64_t{2000}) : 2000;
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			const auto at = (first + i) % rows.size();
			const auto &row = rows[at];
			if (!row.at("eligible").get<bool>() || row.at("human").get<bool>() || row.at("selected") == "tree")
				continue;
			const auto key = row.at("console").get<std::string>() + "/" + row.at("controller").get<std::string>();
			if (sent.contains(key) && now - sent.at(key) < std::chrono::milliseconds(500))
				continue;
			const auto snapshot = world.controller_decision_snapshot({ship, row.at("console"), row.at("controller")});
			if (snapshot.is_null())
				continue;
			const auto revision = snapshot.at("basis").dump();
			const bool changed = basis.contains(key) && basis.at(key) != revision;
			if (row.at("next") > world.ticks() && (row.at("effective") == "tree" || !changed))
				continue;
			if (sent.contains(key) && now - sent.at(key) < std::chrono::milliseconds(changed ? 500 : interval))
				continue;
			batch.push_back(snapshot);
			if (batch.dump().size() > 288000)
			{
				batch.erase(batch.end() - 1);
				break;
			}
			selected.emplace_back(key, revision);
			next_cursor = at + 1;
			if (batch.size() >= (connection ? connection->capacity() : 1))
				break;
		}
		const auto capacity = connection ? connection->capacity() : 1;
		bool waiting = false;
		for (const auto &row : rows)
			waiting = waiting ||
			          (row.at("eligible").get<bool>() && !row.at("human").get<bool>() && row.at("selected") != "tree" &&
			           row.at("effective") != "tree" && row.at("next") > world.ticks());
		if (!batch.empty() && batch.size() < capacity && waiting &&
		    coalesce_until == std::chrono::steady_clock::time_point{})
		{
			coalesce_until = now + std::chrono::milliseconds(500);
			return;
		}
		coalesce_until = {};
		if (!batch.empty())
		{
			cursor = next_cursor;
			for (const auto &[key, revision] : selected)
			{
				sent[key] = now;
				basis[key] = revision;
			}
		}
		if (!batch.empty() && (!connection || !connection->request_batch(batch)))
			for (const auto &snapshot : batch)
				commit(
				    Json{{"kind", "ai-decision"},
				         {"decision", {{"snapshot", snapshot}, {"failed", true}, {"reason", "provider_unavailable"}}}});
	}
};
} // namespace phoenix
