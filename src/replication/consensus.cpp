module;
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

module phoenix.replication;

namespace phoenix
{
namespace
{
constexpr std::uint64_t max_counter = 9007199254740991ULL;
constexpr std::size_t max_entries = 131072, max_packet = 65536, max_payload = 48000;
std::uint64_t number(const WireJson &value)
{
	if (!value.is_number_integer() ||
	    (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid consensus counter");
	auto result = value.get<std::uint64_t>();
	if (result > max_counter)
		throw std::runtime_error("Consensus counter exceeds supported bound");
	return result;
}
bool identity(const std::string &value)
{
	return !value.empty() && value.size() <= 128 &&
	       std::all_of(value.begin(), value.end(),
	                   [](unsigned char c)
	                   {
		                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		                          c == '-' || c == '_' || c == '.';
	                   });
}
} // namespace
struct PeerConsensus::Impl
{
	struct Entry
	{
		std::uint64_t term;
		WireJson value;
	};
	struct Flight
	{
		std::uint64_t previous, end;
	};
	PeerRoster config;
	PeerRole role = PeerRole::follower;
	std::string voted, leader;
	std::uint64_t term = 0, commit = 0, durable_commit = 0, published = 0, generation = 0, sequence = 0, now = 0;
	unsigned election_elapsed = 0, heartbeat_elapsed = 0;
	std::vector<Entry> log;
	std::set<std::string> votes;
	std::set<std::string> pre_votes;
	bool probing = false;
	std::map<std::string, std::uint64_t> next, matched, heard;
	std::map<std::string, std::map<std::uint64_t, Flight>> flights;
	std::vector<PeerDelivery> deliveries, staged;
	std::deque<std::pair<std::string, WireJson>> inbox;
	std::optional<DurableWrite> write;
	bool dirty = true;
	std::size_t dirty_from = 0;
	explicit Impl(PeerRoster roster) : config(std::move(roster))
	{
		std::set<std::string> members(config.members.begin(), config.members.end());
		if (!identity(config.session) || !identity(config.self) || members.size() != config.members.size() ||
		    members.empty() || members.size() > 16 || !members.contains(config.self))
			throw std::runtime_error("Invalid fixed peer roster");
		for (const auto &member : members)
			if (!identity(member))
				throw std::runtime_error("Invalid peer identity");
		std::sort(config.members.begin(), config.members.end());
	}
	std::size_t majority() const
	{
		return config.members.size() / 2 + 1;
	}
	std::uint64_t log_term(std::uint64_t index) const
	{
		return index ? log.at(static_cast<std::size_t>(index - 1)).term : 0;
	}
	unsigned timeout() const
	{
		std::uint64_t hash = 2166136261u;
		for (unsigned char c : config.self)
			hash = (hash ^ c) * 16777619u;
		return 800 + static_cast<unsigned>((hash + term * 317) % 800);
	}
	bool quorum() const
	{
		if (role != PeerRole::leader)
			return false;
		std::size_t count = 1;
		for (const auto &[peer, time] : heard)
			if (peer != config.self && now - time <= 1500)
				++count;
		return count >= majority();
	}
	WireJson snapshot() const
	{
		WireJson entries = WireJson::array();
		for (auto i = dirty_from; i < log.size(); ++i)
			entries.push_back({{"term", log[i].term}, {"value", log[i].value}});
		return {{"schema", 1},         {"session", config.session},
		        {"self", config.self}, {"roster", config.members},
		        {"term", term},        {"voted", voted},
		        {"commit", commit},    {"from", dirty_from},
		        {"entries", entries}};
	}
	void finish()
	{
		if (dirty)
		{
			write = DurableWrite{++generation, snapshot()};
			dirty = false;
			dirty_from = log.size();
		}
		else
		{
			deliveries.insert(deliveries.end(), std::make_move_iterator(staged.begin()),
			                  std::make_move_iterator(staged.end()));
			staged.clear();
		}
	}
	void emit(const std::string &peer, WireJson packet)
	{
		packet["schema"] = 1;
		packet["session"] = config.session;
		packet["from"] = config.self;
		packet["to"] = peer;
		packet["term"] = term;
		staged.push_back({peer, std::move(packet)});
	}
	void follow(std::uint64_t new_term)
	{
		if (new_term > term)
		{
			term = new_term;
			voted.clear();
			dirty = true;
		}
		role = PeerRole::follower;
		leader.clear();
		votes.clear();
		pre_votes.clear();
		probing = false;
		next.clear();
		matched.clear();
		heard.clear();
		flights.clear();
		election_elapsed = 0;
	}
	void append_to(const std::string &peer)
	{
		auto index = std::clamp(next.at(peer), std::uint64_t{1}, static_cast<std::uint64_t>(log.size() + 1));
		WireJson entries = WireJson::array();
		std::size_t bytes = 0;
		for (auto i = index; i <= log.size() && entries.size() < 32; ++i)
		{
			WireJson entry{{"term", log_term(i)}, {"value", log[i - 1].value}};
			auto size = entry.dump().size();
			if (!entries.empty() && bytes + size > max_payload)
				break;
			entries.push_back(std::move(entry));
			bytes += size;
		}
		auto request = ++sequence;
		auto &pending = flights[peer];
		pending[request] = {index - 1, index - 1 + entries.size()};
		while (pending.size() > 16)
			pending.erase(pending.begin());
		emit(peer, {{"type", "append"},
		            {"request", request},
		            {"previous", index - 1},
		            {"previous_term", log_term(index - 1)},
		            {"commit", commit},
		            {"entries", entries}});
	}
	void broadcast()
	{
		for (const auto &peer : config.members)
			if (peer != config.self)
				append_to(peer);
		heartbeat_elapsed = 0;
	}
	void advance_commit()
	{
		std::vector<std::uint64_t> indices{static_cast<std::uint64_t>(log.size())};
		for (const auto &peer : config.members)
			if (peer != config.self)
				indices.push_back(matched[peer]);
		std::sort(indices.begin(), indices.end(), std::greater<>());
		auto candidate = indices[majority() - 1];
		if (candidate > commit && log_term(candidate) == term)
		{
			commit = candidate;
			dirty = true;
			broadcast();
		}
	}
	void become_leader()
	{
		role = PeerRole::leader;
		leader = config.self;
		probing = false;
		election_elapsed = 0;
		for (const auto &peer : config.members)
			if (peer != config.self)
			{
				next[peer] = log.size() + 1;
				matched[peer] = 0;
				if (votes.contains(peer))
					heard[peer] = now;
			}
		// A current-term no-op commits an inherited prefix before gameplay resumes.
		if (log.size() >= max_entries)
		{
			role = PeerRole::failed;
			staged.clear();
			return;
		}
		dirty_from = std::min(dirty_from, log.size());
		log.push_back({term, {{"kind", "consensus-noop"}}});
		dirty = true;
		broadcast();
		advance_commit();
	}
	void elect()
	{
		if (term == max_counter)
		{
			role = PeerRole::failed;
			return;
		}
		++term;
		voted = config.self;
		role = PeerRole::candidate;
		leader.clear();
		probing = false;
		votes = {config.self};
		election_elapsed = 0;
		dirty = true;
		for (const auto &peer : config.members)
			if (peer != config.self)
				emit(peer, {{"type", "vote"}, {"last", log.size()}, {"last_term", log_term(log.size())}});
		if (votes.size() >= majority())
			become_leader();
	}
	void probe()
	{
		if (term == max_counter)
		{
			role = PeerRole::failed;
			return;
		}
		probing = true;
		pre_votes = {config.self};
		election_elapsed = 0;
		for (const auto &peer : config.members)
			if (peer != config.self)
				emit(peer, {{"type", "pre-vote"},
				            {"proposed", term + 1},
				            {"last", log.size()},
				            {"last_term", log_term(log.size())}});
		if (pre_votes.size() >= majority())
			elect();
	}
	bool valid(const std::string &sender, const WireJson &packet) const
	{
		try
		{
			if (!packet.is_object() || packet.dump().size() > max_packet || packet.at("schema") != 1 ||
			    packet.at("session") != config.session || packet.at("from") != sender ||
			    packet.at("to") != config.self || sender == config.self ||
			    !std::binary_search(config.members.begin(), config.members.end(), sender))
				return false;
			auto packet_term = number(packet.at("term"));
			const std::string type = packet.at("type");
			if (type == "vote" || type == "pre-vote")
			{
				auto last = number(packet.at("last")), last_term = number(packet.at("last_term"));
				return last <= max_entries && last_term <= packet_term && ((last == 0) == (last_term == 0)) &&
				       (type == "vote" || number(packet.at("proposed")) == packet_term + 1);
			}
			if (type == "pre-voted")
				return packet.at("granted").is_boolean() && number(packet.at("proposed")) > 0;
			if (type == "voted")
				return packet.at("granted").is_boolean();
			if (type == "appended")
				return packet.at("success").is_boolean() && number(packet.at("request")) > 0 &&
				       number(packet.at("match")) <= max_entries && number(packet.at("hint")) <= max_entries + 1;
			if (type != "append" || number(packet.at("request")) == 0 || number(packet.at("commit")) > max_entries)
				return false;
			auto previous = number(packet.at("previous")), previous_term = number(packet.at("previous_term"));
			if (previous > max_entries || previous_term > packet_term || ((previous == 0) != (previous_term == 0)) ||
			    !packet.at("entries").is_array() || packet.at("entries").size() > 32 ||
			    previous + packet.at("entries").size() > max_entries)
				return false;
			for (const auto &entry : packet.at("entries"))
			{
				auto entry_term = number(entry.at("term"));
				if (!entry_term || entry_term < previous_term || entry_term > packet_term ||
				    entry.at("value").dump().size() > max_payload)
					return false;
				previous_term = entry_term;
			}
			return true;
		}
		catch (const std::exception &)
		{
			return false;
		}
	}
	void handle(const std::string &sender, const WireJson &packet)
	{
		auto incoming_term = number(packet.at("term"));
		const std::string type = packet.at("type");
		if (type == "pre-vote")
		{
			auto last = number(packet.at("last")), last_term = number(packet.at("last_term"));
			bool fresh = last_term > log_term(log.size()) || (last_term == log_term(log.size()) && last >= log.size());
			bool grant = number(packet.at("proposed")) > term && fresh && role != PeerRole::leader &&
			             (probing || leader.empty() || election_elapsed >= timeout());
			emit(sender, {{"type", "pre-voted"}, {"proposed", packet.at("proposed")}, {"granted", grant}});
			return;
		}
		if (type == "pre-voted")
		{
			if (probing && number(packet.at("proposed")) == term + 1 && packet.at("granted").get<bool>())
			{
				pre_votes.insert(sender);
				if (pre_votes.size() >= majority())
					elect();
			}
			return;
		}
		if (incoming_term > term)
			follow(incoming_term);
		if (type == "vote")
		{
			auto last = number(packet.at("last")), last_term = number(packet.at("last_term"));
			bool fresh = last_term > log_term(log.size()) || (last_term == log_term(log.size()) && last >= log.size());
			bool grant = incoming_term == term && fresh && (voted.empty() || voted == sender);
			if (grant)
			{
				if (voted != sender)
				{
					voted = sender;
					dirty = true;
				}
				election_elapsed = 0;
			}
			emit(sender, {{"type", "voted"}, {"granted", grant}});
		}
		else if (type == "voted")
		{
			if (incoming_term == term && role == PeerRole::candidate && packet.at("granted").get<bool>())
			{
				votes.insert(sender);
				if (votes.size() >= majority())
					become_leader();
			}
		}
		else if (type == "append")
		{
			auto previous = number(packet.at("previous"));
			bool accept = incoming_term == term;
			if (accept)
			{
				if (role != PeerRole::follower)
					follow(term);
				leader = sender;
				probing = false;
				pre_votes.clear();
				election_elapsed = 0;
			}
			accept = accept && previous <= log.size() && log_term(previous) == number(packet.at("previous_term"));
			auto hint = std::min<std::uint64_t>(previous, log.size() + 1);
			if (accept)
			{
				auto index = previous;
				for (const auto &entry : packet.at("entries"))
				{
					++index;
					if (index <= log.size() &&
					    (log_term(index) != number(entry.at("term")) || log[index - 1].value != entry.at("value")))
					{
						if (index <= commit)
						{
							accept = false;
							break;
						}
						log.resize(index - 1);
						dirty_from = std::min(dirty_from, log.size());
						dirty = true;
					}
					if (index > log.size())
					{
						dirty_from = std::min(dirty_from, log.size());
						log.push_back({number(entry.at("term")), entry.at("value")});
						dirty = true;
					}
				}
				if (accept)
				{
					auto agreed = std::min(number(packet.at("commit")), index);
					if (agreed > commit)
					{
						commit = agreed;
						dirty = true;
					}
					hint = index + 1;
				}
			}
			emit(sender, {{"type", "appended"},
			              {"request", packet.at("request")},
			              {"success", accept},
			              {"match", accept ? previous + packet.at("entries").size() : 0},
			              {"hint", std::max<std::uint64_t>(1, hint)}});
		}
		else if (type == "appended" && incoming_term == term && role == PeerRole::leader)
		{
			auto request = number(packet.at("request"));
			auto &pending = flights[sender];
			auto found = pending.find(request);
			if (found == pending.end())
				return;
			auto flight = found->second;
			bool newest = request == pending.rbegin()->first;
			pending.erase(found);
			if (packet.at("success").get<bool>())
			{
				auto match = number(packet.at("match"));
				if (match != flight.end)
					return;
				heard[sender] = now;
				matched[sender] = std::max(matched[sender], match);
				next[sender] = matched[sender] + 1;
				auto before = commit;
				advance_commit();
				if (commit == before && next[sender] <= log.size())
					append_to(sender);
			}
			else if (newest && flight.previous >= matched[sender])
			{
				next[sender] = std::max(matched[sender] + 1, std::min(flight.previous, number(packet.at("hint"))));
				append_to(sender);
			}
		}
	}
};
PeerConsensus::PeerConsensus(PeerRoster roster, const WireJson &restored)
    : impl_(std::make_unique<Impl>(std::move(roster)))
{
	auto &s = *impl_;
	if (!restored.is_null())
	{
		if (restored.dump().size() > 64 * 1024 * 1024 || restored.at("schema") != 1 ||
		    restored.at("session") != s.config.session || restored.at("self") != s.config.self ||
		    restored.at("roster") != WireJson(s.config.members) || !restored.at("log").is_array() ||
		    restored.at("log").size() > max_entries)
			throw std::runtime_error("Durable consensus state does not match fixed roster/session");
		s.term = number(restored.at("term"));
		s.voted = restored.at("voted").get<std::string>();
		s.commit = number(restored.at("commit"));
		if (!s.voted.empty() && !std::binary_search(s.config.members.begin(), s.config.members.end(), s.voted))
			throw std::runtime_error("Durable vote names an unknown peer");
		std::uint64_t previous = 0;
		for (const auto &entry : restored.at("log"))
		{
			auto term = number(entry.at("term"));
			if (!term || term < previous || term > s.term || entry.at("value").dump().size() > max_payload)
				throw std::runtime_error("Invalid durable log");
			s.log.push_back({term, entry.at("value")});
			previous = term;
		}
		if (s.commit > s.log.size())
			throw std::runtime_error("Durable commit exceeds log");
		s.durable_commit = s.commit;
		s.dirty = false;
		s.dirty_from = s.log.size();
	}
	s.finish();
}
PeerConsensus::~PeerConsensus() = default;
void PeerConsensus::elapse(unsigned milliseconds)
{
	auto &s = *impl_;
	if (s.write || s.role == PeerRole::failed)
		return;
	milliseconds = std::min(milliseconds, 1000u);
	s.now += milliseconds;
	s.election_elapsed += milliseconds;
	s.heartbeat_elapsed += milliseconds;
	if (s.role == PeerRole::leader)
	{
		if (s.election_elapsed > 1500 && !s.quorum())
			s.follow(s.term);
		else if (s.heartbeat_elapsed >= 150)
			s.broadcast();
	}
	else if (s.election_elapsed >= s.timeout())
		s.probe();
	s.finish();
}
bool PeerConsensus::receive(const std::string &sender, const WireJson &packet)
{
	auto &s = *impl_;
	if (s.role == PeerRole::failed || !s.valid(sender, packet))
		return false;
	if (s.write)
	{
		if (s.inbox.size() >= 256)
			return false;
		s.inbox.emplace_back(sender, packet);
		return true;
	}
	s.handle(sender, packet);
	s.finish();
	return true;
}
bool PeerConsensus::propose(const WireJson &value)
{
	auto &s = *impl_;
	if (s.write || !s.quorum() || s.log.size() >= max_entries || value.dump().size() > max_payload)
		return false;
	s.dirty_from = std::min(s.dirty_from, s.log.size());
	s.log.push_back({s.term, value});
	s.dirty = true;
	s.broadcast();
	s.advance_commit();
	s.finish();
	return true;
}
std::optional<DurableWrite> PeerConsensus::pending_write() const
{
	return impl_->write;
}
void PeerConsensus::persisted(std::uint64_t generation, bool success)
{
	auto &s = *impl_;
	if (!s.write || s.write->generation != generation)
		return;
	const auto persisted_commit = number(s.write->transaction.at("commit"));
	s.write.reset();
	if (!success)
	{
		s.role = PeerRole::failed;
		s.leader.clear();
		s.staged.clear();
		s.deliveries.clear();
		s.inbox.clear();
		return;
	}
	s.durable_commit = persisted_commit;
	s.finish();
	while (!s.write && !s.inbox.empty())
	{
		auto [sender, packet] = std::move(s.inbox.front());
		s.inbox.pop_front();
		s.handle(sender, packet);
		s.finish();
	}
}
std::vector<PeerDelivery> PeerConsensus::take_deliveries()
{
	auto result = std::move(impl_->deliveries);
	impl_->deliveries.clear();
	return result;
}
std::vector<CommittedEntry> PeerConsensus::take_committed()
{
	auto &s = *impl_;
	std::vector<CommittedEntry> result;
	if (s.role == PeerRole::failed)
		return result;
	while (s.published < s.durable_commit)
	{
		const auto &entry = s.log[s.published++];
		result.push_back({s.published, entry.term, entry.value});
	}
	return result;
}
PeerRole PeerConsensus::role() const
{
	return impl_->role;
}
std::string PeerConsensus::leader() const
{
	return impl_->leader;
}
bool PeerConsensus::has_quorum() const
{
	return impl_->quorum();
}
std::uint64_t PeerConsensus::term() const
{
	return impl_->term;
}
std::uint64_t PeerConsensus::commit_index() const
{
	return impl_->durable_commit;
}
void apply_peer_write(WireJson &recovered, const WireJson &transaction)
{
	const auto &t = transaction;
	if (!t.is_object() || t.at("schema") != 1 || !t.at("entries").is_array() || t.dump().size() > 2 * 1024 * 1024)
		throw std::runtime_error("Invalid durable peer transaction");
	const auto from = number(t.at("from")), term = number(t.at("term")), commit = number(t.at("commit"));
	PeerRoster config{t.at("session"), t.at("self"), t.at("roster").get<std::vector<std::string>>()};
	if (!identity(config.self) || !identity(config.session) || config.members.empty() || config.members.size() > 16 ||
	    std::find(config.members.begin(), config.members.end(), config.self) == config.members.end() ||
	    !std::is_sorted(config.members.begin(), config.members.end()) ||
	    std::adjacent_find(config.members.begin(), config.members.end()) != config.members.end())
		throw std::runtime_error("Invalid durable peer identity/roster");
	for (const auto &peer : config.members)
		if (!identity(peer))
			throw std::runtime_error("Invalid durable peer identity");
	const auto vote = t.at("voted").get<std::string>();
	if (!vote.empty() && !std::binary_search(config.members.begin(), config.members.end(), vote))
		throw std::runtime_error("Invalid durable peer vote");
	std::uint64_t previous_term = 0;
	if (recovered.is_null())
	{
		if (from != 0)
			throw std::runtime_error("Missing durable log prefix");
	}
	else
	{
		if (t.at("session") != recovered.at("session") || t.at("self") != recovered.at("self") ||
		    t.at("roster") != recovered.at("roster") || term < number(recovered.at("term")) ||
		    from > recovered.at("log").size() || from < number(recovered.at("commit")) ||
		    commit < number(recovered.at("commit")))
			throw std::runtime_error("Durable peer transaction changes identity or committed history");
		if (term == number(recovered.at("term")) && recovered.at("voted") != "" &&
		    t.at("voted") != recovered.at("voted"))
			throw std::runtime_error("Conflicting durable votes in the same term");
		if (from)
			previous_term = number(recovered.at("log").at(from - 1).at("term"));
	}
	if (from + t.at("entries").size() > max_entries || commit > from + t.at("entries").size())
		throw std::runtime_error("Durable commit/log bounds mismatch");
	for (const auto &entry : t.at("entries"))
	{
		auto entry_term = number(entry.at("term"));
		if (!entry_term || entry_term < previous_term || entry_term > term ||
		    entry.at("value").dump().size() > max_payload)
			throw std::runtime_error("Invalid durable log suffix");
		previous_term = entry_term;
	}
	if (recovered.is_null())
		recovered = {{"schema", 1},
		             {"session", config.session},
		             {"self", config.self},
		             {"roster", config.members},
		             {"log", WireJson::array()}};
	auto &log = recovered.at("log").get_ref<WireJson::array_t &>();
	log.resize(static_cast<std::size_t>(from));
	for (const auto &entry : t.at("entries"))
		log.push_back(entry);
	recovered["term"] = term;
	recovered["voted"] = vote;
	recovered["commit"] = commit;
}
} // namespace phoenix
