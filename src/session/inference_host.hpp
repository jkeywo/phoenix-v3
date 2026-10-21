#pragma once
// Host-local scheduling. Every state change goes through the supplied commit path.
namespace phoenix
{
struct InferenceHost
{
	std::shared_ptr<InferenceConnection> connection;
	std::size_t cursor = 0;
	std::map<std::string, std::string> basis;
	std::map<std::string, std::chrono::steady_clock::time_point> sent;
	void reset()
	{
		sent.clear();
		basis.clear();
		cursor = 0;
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
		const auto rows = world.controller_ai(ship);
		const auto now = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			const auto at = (cursor + i) % rows.size();
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
			if (sent.contains(key) && now - sent.at(key) < std::chrono::milliseconds(changed ? 500 : 2000))
				continue;
			sent[key] = now;
			basis[key] = revision;
			cursor = at + 1;
			if (snapshot.is_null())
				continue;
			if (!connection || !connection->request(snapshot))
				commit(
				    Json{{"kind", "ai-decision"},
				         {"decision", {{"snapshot", snapshot}, {"failed", true}, {"reason", "provider_unavailable"}}}});
			break;
		}
	}
};
} // namespace phoenix
