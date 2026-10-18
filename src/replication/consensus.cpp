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
#include <span>

module phoenix.replication;

namespace phoenix
{
namespace
{
constexpr std::uint64_t max_counter = 9007199254740991ULL;
constexpr std::size_t max_entries = 131072, max_packet = 65536, max_payload = 48000;
constexpr std::size_t max_snapshot = 32 * 1024 * 1024;
std::string digest(const std::string &bytes)
{
	return sha256(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
}
std::string hex_chunk(const std::string &bytes, std::size_t offset)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for (std::size_t i = offset; i < std::min(bytes.size(), offset + 16000); ++i)
	{
		auto c = static_cast<unsigned char>(bytes[i]);
		result += digits[c >> 4];
		result += digits[c & 15];
	}
	return result;
}
std::string unhex(const std::string &text)
{
	if (text.size() > 32000 || text.size() % 2)
		throw std::runtime_error("Invalid snapshot chunk");
	auto digit = [](char c) -> unsigned
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		throw std::runtime_error("Invalid snapshot encoding");
	};
	std::string result;
	for (std::size_t i = 0; i < text.size(); i += 2)
		result += static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1]));
	return result;
}
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
	std::uint64_t base = 0, base_term = 0;
	WireJson checkpoint_state;
	std::string snapshot_bytes, snapshot_hash;
	std::uint64_t journal_bytes = 0;
	bool replacing = false;
	struct SnapshotFlight
	{
		std::uint64_t request = 0, index = 0;
		std::size_t offset = 0, end = 0;
		std::string hash;
	};
	std::map<std::string, SnapshotFlight> snapshots;
	struct Incoming
	{
		std::string sender, hash, bytes;
		std::uint64_t index = 0, term = 0;
		std::size_t size = 0;
	} incoming;
	std::uint64_t last() const
	{
		return base + log.size();
	}
	void set_checkpoint(std::uint64_t index, std::uint64_t checkpoint_term, WireJson state)
	{
		if (index >= base && index <= last() && log_term(index) == checkpoint_term)
			log.erase(log.begin(), log.begin() + static_cast<std::ptrdiff_t>(index - base));
		else
			log.clear();
		base = index;
		base_term = checkpoint_term;
		checkpoint_state = std::move(state);
		snapshot_bytes = checkpoint_state.dump();
		snapshot_hash = digest(snapshot_bytes);
		commit = std::max(commit, base);
		dirty = true;
		dirty_from = 0;
		replacing = true;
		snapshots.clear();
	}
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
		if (!valid_identifier(config.session) || !valid_identifier(config.self) ||
		    members.size() != config.members.size() || members.empty() || members.size() > 16 ||
		    !members.contains(config.self))
			throw std::runtime_error("Invalid fixed peer roster");
		for (const auto &member : members)
			if (!valid_identifier(member))
				throw std::runtime_error("Invalid peer identity");
		std::sort(config.members.begin(), config.members.end());
	}
	std::size_t majority() const
	{
		return config.members.size() / 2 + 1;
	}
	std::uint64_t log_term(std::uint64_t index) const
	{
		return index == base ? base_term : log.at(static_cast<std::size_t>(index - base - 1)).term;
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
		WireJson transaction{{"schema", 2},         {"session", config.session},
		                     {"self", config.self}, {"roster", config.members},
		                     {"term", term},        {"voted", voted},
		                     {"commit", commit},    {"from", base + dirty_from},
		                     {"base", base},        {"base_term", base_term},
		                     {"entries", entries}};
		if (replacing)
			transaction["snapshot"] = checkpoint_state;
		return transaction;
	}
	void finish()
	{
		if (dirty)
		{
			write = DurableWrite{++generation, snapshot()};
			replacing = false;
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
		snapshots.clear();
		incoming = {};
		election_elapsed = 0;
	}
	void append_to(const std::string &peer)
	{
		if (next.at(peer) <= base)
		{
			auto &flight = snapshots[peer];
			if (flight.index != base || flight.hash != snapshot_hash)
				flight = {0, base, 0, 0, snapshot_hash};
			if (!flight.request)
				flight.request = ++sequence;
			flight.end = std::min(snapshot_bytes.size(), flight.offset + 16000);
			emit(peer, {{"type", "snapshot"},
			            {"request", flight.request},
			            {"index", base},
			            {"snapshot_term", base_term},
			            {"hash", snapshot_hash},
			            {"size", snapshot_bytes.size()},
			            {"offset", flight.offset},
			            {"data", hex_chunk(snapshot_bytes, flight.offset)}});
			return;
		}
		auto index = std::clamp(next.at(peer), base + 1, last() + 1);
		WireJson entries = WireJson::array();
		std::size_t bytes = 0;
		for (auto i = index; i <= last() && entries.size() < 32; ++i)
		{
			WireJson entry{{"term", log_term(i)}, {"value", log[i - base - 1].value}};
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
		std::vector<std::uint64_t> indices{last()};
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
				next[peer] = last() + 1;
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
				emit(peer, {{"type", "vote"}, {"last", last()}, {"last_term", log_term(this->last())}});
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
				            {"last", last()},
				            {"last_term", log_term(this->last())}});
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
				return last <= max_counter && last_term <= packet_term && ((last == 0) == (last_term == 0)) &&
				       (type == "vote" || number(packet.at("proposed")) == packet_term + 1);
			}
			if (type == "pre-voted")
				return packet.at("granted").is_boolean() && number(packet.at("proposed")) > 0;
			if (type == "voted")
				return packet.at("granted").is_boolean();
			if (type == "snapshot")
			{
				const auto index = number(packet.at("index")), st = number(packet.at("snapshot_term")),
				           size = number(packet.at("size")), offset = number(packet.at("offset"));
				const auto chunk = unhex(packet.at("data"));
				return number(packet.at("request")) > 0 && index > 0 && st > 0 && st <= packet_term &&
				       valid_hex_token(packet.at("hash").get<std::string>()) && size > 0 && size <= max_snapshot &&
				       offset < size && !chunk.empty() && offset + chunk.size() <= size;
			}
			if (type == "snapshot-ack")
				return number(packet.at("request")) > 0 && number(packet.at("index")) > 0 &&
				       number(packet.at("offset")) <= max_snapshot &&
				       valid_hex_token(packet.at("hash").get<std::string>());
			if (type == "appended")
				return packet.at("success").is_boolean() && number(packet.at("request")) > 0 &&
				       number(packet.at("match")) < max_counter && number(packet.at("hint")) <= max_counter;
			if (type != "append" || number(packet.at("request")) == 0 || number(packet.at("commit")) >= max_counter)
				return false;
			auto previous = number(packet.at("previous")), previous_term = number(packet.at("previous_term"));
			if (previous >= max_counter || previous_term > packet_term || ((previous == 0) != (previous_term == 0)) ||
			    !packet.at("entries").is_array() || packet.at("entries").size() > 32 ||
			    previous + packet.at("entries").size() >= max_counter)
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
			bool fresh =
			    last_term > log_term(this->last()) || (last_term == log_term(this->last()) && last >= this->last());
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
			bool fresh =
			    last_term > log_term(this->last()) || (last_term == log_term(this->last()) && last >= this->last());
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
			accept = accept && previous >= base && previous <= last() &&
			         log_term(previous) == number(packet.at("previous_term"));
			auto hint = previous < base ? base + 1 : std::min<std::uint64_t>(previous, last() + 1);
			if (accept)
			{
				auto index = previous;
				for (const auto &entry : packet.at("entries"))
				{
					++index;
					if (index <= last() && (log_term(index) != number(entry.at("term")) ||
					                        log[index - base - 1].value != entry.at("value")))
					{
						if (index <= commit)
						{
							accept = false;
							break;
						}
						log.resize(index - base - 1);
						dirty_from = std::min(dirty_from, log.size());
						dirty = true;
					}
					if (index > last())
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
		else if (type == "snapshot" && incoming_term == term)
		{
			if (role != PeerRole::follower)
				follow(term);
			leader = sender;
			probing = false;
			pre_votes.clear();
			election_elapsed = 0;
			const auto index = number(packet.at("index")), st = number(packet.at("snapshot_term"));
			const std::string hash = packet.at("hash");
			const auto size = number(packet.at("size")), offset = number(packet.at("offset"));
			auto restart_transfer = [&]
			{
				emit(sender, {{"type", "snapshot-ack"},
				              {"request", packet.at("request")},
				              {"index", index},
				              {"hash", hash},
				              {"offset", 0}});
			};
			std::size_t acknowledged = 0;
			if (index <= commit)
				acknowledged = offset + unhex(packet.at("data")).size();
			else
			{
				if (incoming.sender != sender || incoming.hash != hash || incoming.index != index ||
				    incoming.term != st || incoming.size != size)
				{
					if (offset)
					{
						restart_transfer();
						return;
					}
					incoming = {sender, hash, {}, index, st, static_cast<std::size_t>(size)};
				}
				const auto chunk = unhex(packet.at("data"));
				if (offset == incoming.bytes.size())
					incoming.bytes += chunk;
				acknowledged = incoming.bytes.size();
				if (acknowledged == size)
				{
					if (digest(incoming.bytes) != hash)
					{
						incoming = {};
						restart_transfer();
						return;
					}
					auto state = WireJson::parse(incoming.bytes);
					if (!state.is_object())
					{
						incoming = {};
						return;
					}
					set_checkpoint(index, st, std::move(state));
					incoming = {};
				}
			}
			emit(sender, {{"type", "snapshot-ack"},
			              {"request", packet.at("request")},
			              {"index", index},
			              {"hash", hash},
			              {"offset", acknowledged}});
		}
		else if (type == "snapshot-ack" && incoming_term == term && role == PeerRole::leader)
		{
			auto found = snapshots.find(sender);
			if (found == snapshots.end())
				return;
			auto &flight = found->second;
			if (packet.at("request") != flight.request || packet.at("index") != flight.index ||
			    packet.at("hash") != flight.hash ||
			    (number(packet.at("offset")) != flight.end && number(packet.at("offset")) != 0))
				return;
			heard[sender] = now;
			flight.offset = number(packet.at("offset"));
			flight.request = 0;
			if (flight.offset == snapshot_bytes.size())
			{
				matched[sender] = std::max(matched[sender], base);
				next[sender] = matched[sender] + 1;
				snapshots.erase(found);
			}
			append_to(sender);
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
				if (commit == before && next[sender] <= last())
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
		if (restored.dump().size() > 64 * 1024 * 1024 || (restored.at("schema") != 1 && restored.at("schema") != 2) ||
		    restored.at("session") != s.config.session || restored.at("self") != s.config.self ||
		    restored.at("roster") != WireJson(s.config.members) || !restored.at("log").is_array() ||
		    restored.at("log").size() > max_entries)
			throw std::runtime_error("Durable consensus state does not match fixed roster/session");
		s.term = number(restored.at("term"));
		s.voted = restored.at("voted").get<std::string>();
		s.commit = number(restored.at("commit"));
		if (!s.voted.empty() && !std::binary_search(s.config.members.begin(), s.config.members.end(), s.voted))
			throw std::runtime_error("Durable vote names an unknown peer");
		s.base = number(restored.value("base", WireJson(0)));
		s.base_term = number(restored.value("base_term", WireJson(0)));
		if (s.base)
		{
			s.checkpoint_state = restored.at("snapshot");
			s.snapshot_bytes = s.checkpoint_state.dump();
			s.snapshot_hash = digest(s.snapshot_bytes);
			if (!s.checkpoint_state.is_object() || s.snapshot_bytes.size() > max_snapshot || !s.base_term ||
			    s.base_term > s.term)
				throw std::runtime_error("Invalid durable checkpoint");
		}
		else if (s.base_term)
			throw std::runtime_error("Invalid initial checkpoint term");
		s.journal_bytes = restored.value("journal_bytes", std::uint64_t{0});
		std::uint64_t previous = s.base_term;
		for (const auto &entry : restored.at("log"))
		{
			auto term = number(entry.at("term"));
			if (!term || term < previous || term > s.term || entry.at("value").dump().size() > max_payload)
				throw std::runtime_error("Invalid durable log");
			s.log.push_back({term, entry.at("value")});
			previous = term;
		}
		if (s.commit < s.base || s.commit > s.last() || s.last() >= max_counter)
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
	if (s.write->transaction.contains("snapshot"))
		s.journal_bytes = 0;
	else
		s.journal_bytes += s.write->transaction.dump().size() + 66;
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
	if (s.published < s.base && s.base <= s.durable_commit)
	{
		result.push_back({s.base, s.base_term, {{"kind", "consensus-snapshot"}, {"state", s.checkpoint_state}}});
		s.published = s.base;
	}
	if (s.published < s.base)
		return result;
	while (s.published < s.durable_commit)
	{
		const auto &entry = s.log[s.published++ - s.base];
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
bool PeerConsensus::checkpoint_due() const
{
	const auto &s = *impl_;
	return !s.write && s.role != PeerRole::failed && s.published == s.commit && s.commit > 0 &&
	       (s.commit - s.base >= 4096 || s.journal_bytes >= 32 * 1024 * 1024);
}
bool PeerConsensus::checkpoint(const WireJson &state)
{
	auto &s = *impl_;
	if (s.write || s.role == PeerRole::failed || s.published != s.commit || s.commit != s.durable_commit || !s.commit ||
	    !state.is_object() || state.dump().size() > max_snapshot)
		return false;
	s.set_checkpoint(s.commit, s.log_term(s.commit), state);
	s.finish();
	return true;
}
std::uint64_t PeerConsensus::checkpoint_index() const
{
	return impl_->base;
}
void apply_peer_write(WireJson &recovered, const WireJson &t)
{
	if (!t.is_object() || (t.at("schema") != 1 && t.at("schema") != 2) || !t.at("entries").is_array() ||
	    t.dump().size() > 64 * 1024 * 1024)
		throw std::runtime_error("Invalid durable peer transaction");
	const auto from = number(t.at("from")), term = number(t.at("term")), commit = number(t.at("commit"));
	const auto base = number(t.value("base", WireJson(0))), base_term = number(t.value("base_term", WireJson(0)));
	const bool replace = t.contains("snapshot");
	const auto old_base = recovered.is_null() ? 0 : number(recovered.value("base", WireJson(0)));
	const auto old_commit = recovered.is_null() ? 0 : number(recovered.at("commit"));
	if (!recovered.is_null())
	{
		if (t.at("session") != recovered.at("session") || t.at("self") != recovered.at("self") ||
		    t.at("roster") != recovered.at("roster") || term < number(recovered.at("term")) || commit < old_commit)
			throw std::runtime_error("Durable peer transaction changes identity or committed history");
		if (term == number(recovered.at("term")) && recovered.at("voted") != "" &&
		    t.at("voted") != recovered.at("voted"))
			throw std::runtime_error("Conflicting durable votes in the same term");
	}
	if (replace)
	{
		if (!base || base < old_commit || from != base || commit < base || !t.at("snapshot").is_object())
			throw std::runtime_error("Invalid durable checkpoint replacement");
		if (!recovered.is_null() && base > old_base && base <= old_base + recovered.at("log").size() &&
		    base <= old_commit && number(recovered.at("log").at(base - old_base - 1).at("term")) != base_term)
			throw std::runtime_error("Checkpoint changes committed term");
	}
	else if (base != old_base || from < old_commit || from < base ||
	         (recovered.is_null() ? from != 0 : from > old_base + recovered.at("log").size()))
		throw std::runtime_error("Missing durable log prefix");
	PeerRoster config{t.at("session"), t.at("self"), t.at("roster").get<std::vector<std::string>>()};
	PeerConsensus validate_roster(config);
	if (!std::is_sorted(config.members.begin(), config.members.end()))
		throw std::runtime_error("Unsorted durable roster");
	const auto vote = t.at("voted").get<std::string>();
	if (!vote.empty() && std::find(config.members.begin(), config.members.end(), vote) == config.members.end())
		throw std::runtime_error("Invalid durable vote");
	if (from - base > max_entries || t.at("entries").size() > max_entries - (from - base) || commit < base ||
	    commit > from + t.at("entries").size() || from + t.at("entries").size() >= max_counter)
		throw std::runtime_error("Durable log bounds mismatch");
	if ((base == 0) != (base_term == 0) || base_term > term ||
	    (!replace && !recovered.is_null() && base_term != number(recovered.value("base_term", WireJson(0)))))
		throw std::runtime_error("Invalid checkpoint term");
	auto previous = from > base ? number(recovered.at("log").at(from - base - 1).at("term")) : base_term;
	for (const auto &entry : t.at("entries"))
	{
		const auto entry_term = number(entry.at("term"));
		if (!entry_term || entry_term < previous || entry_term > term || entry.at("value").dump().size() > max_payload)
			throw std::runtime_error("Invalid durable log suffix");
		previous = entry_term;
	}
	if (replace && t.at("snapshot").dump().size() > max_snapshot)
		throw std::runtime_error("Checkpoint exceeds limit");
	if (replace || recovered.is_null())
		recovered = {{"schema", 2},
		             {"session", t.at("session")},
		             {"self", t.at("self")},
		             {"roster", t.at("roster")},
		             {"log", WireJson::array()}};
	auto &log = recovered.at("log").get_ref<WireJson::array_t &>();
	log.resize(static_cast<std::size_t>(from - base));
	for (const auto &entry : t.at("entries"))
		log.push_back(entry);
	recovered["schema"] = 2;
	recovered["term"] = term;
	recovered["voted"] = vote;
	recovered["commit"] = commit;
	recovered["base"] = base;
	recovered["base_term"] = base_term;
	if (replace)
		recovered["snapshot"] = t.at("snapshot");
}
} // namespace phoenix
