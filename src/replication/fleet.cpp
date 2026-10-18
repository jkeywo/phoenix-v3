module;
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.fleet;
import phoenix.persistence;
import phoenix.wire;

namespace phoenix
{
struct FleetCommit::Impl
{
	struct Seen
	{
		std::string boot;
		std::uint64_t time = 0, commit = 0;
		bool playing = false;
	};
	struct Request
	{
		std::string origin, boot, id;
		WireJson value;
	};
	FleetOptions options;
	std::unique_ptr<PeerTransport> transport;
	std::unique_ptr<DurableConsensus> journal;
	PeerConsensus *core = nullptr;
	std::map<std::string, Seen> seen;
	std::map<std::string, std::string> presence;
	std::map<std::string, std::set<std::string>> retired;
	std::deque<Request> requests;
	std::map<std::string, Request> own_requests;
	std::set<std::string> completed;
	std::vector<CommittedEntry> committed;
	std::string boot = random_token(), error, leader_status_peer, inflight_id;
	std::uint64_t admission_generation = 0;
	std::uint64_t now = 0, last_hello = 0, last_retry = 0, status_time = 0, status_term = 0, status_commit = 0,
	              barrier_term = 0, observed_term = 0;
	bool playing = false, broken = false, inflight = false, was_available = false, bootstrap = false, active = true;
	std::chrono::steady_clock::time_point previous = std::chrono::steady_clock::now();
	explicit Impl(FleetOptions config, std::unique_ptr<PeerTransport> route)
	    : options(std::move(config)), transport(std::move(route))
	{
		active = !options.activation_required;
		if (!valid_hex_token(options.key) || !valid_hex_token(options.fingerprint) || options.journal.empty() ||
		    options.link.self != options.roster.self)
			throw std::runtime_error("Invalid fleet key, fingerprint, journal or route identity");
		std::sort(options.roster.members.begin(), options.roster.members.end());
		// Reuse the actor's complete fixed-roster validation before opening platform resources.
		PeerConsensus validate(options.roster);
		verify_link_options(options.link);
		if (!transport)
			transport = open_peer_transport(options.link);
		journal = std::make_unique<DurableConsensus>(options.roster, options.journal,
		                                             options.require_existing ? JournalRecovery::require
		                                                                      : JournalRecovery::allow);
	}
	bool is_member(const std::string &peer) const
	{
		return std::binary_search(options.roster.members.begin(), options.roster.members.end(), peer);
	}
	bool leads() const
	{
		return active && core && core->role() == PeerRole::leader && core->has_quorum() &&
		       barrier_term == core->term() && !broken;
	}
	bool authority() const
	{
		if (!active || !core || broken || !playing)
			return false;
		auto own = presence.find(options.roster.self);
		if (own == presence.end() || own->second != boot)
			return false;
		if (leads())
			return true;
		return !leader_status_peer.empty() && core->leader() == leader_status_peer && core->term() == status_term &&
		       now - status_time <= 750;
	}
	bool ready() const
	{
		// Current-incarnation presence is itself committed after verified catch-up.
		// Once admitted, a follower normally trails a continuously ticking leader;
		// chasing every newer advertised commit would starve Console admission.
		// Requests still execute only after quorum commitment, and authority loss
		// retires queued requests before a new incarnation can serve again.
		return authority();
	}
	void send(const std::string &peer, const std::string &type, const WireJson &body)
	{
		transport->send(peer,
		                encode_packet({options.roster.session, options.roster.self, peer, type, body}, options.key));
	}
	void enqueue(Request request)
	{
		if (completed.contains(request.id))
			return;
		for (const auto &value : requests)
			if (value.id == request.id)
				return;
		if (requests.size() < 128)
			requests.push_back(std::move(request));
	}
	void handle(const LinkNotice &notice)
	{
		if (!active || notice.kind != LinkEvent::packet || !is_member(notice.peer) ||
		    notice.peer == options.roster.self)
			return;
		const auto packet =
		    decode_packet(notice.data, notice.peer, options.roster.self, options.roster.session, options.key);
		if (!packet)
			return;
		const auto &body = packet->body;
		if (packet->type == "fleet-hello")
		{
			const std::string fingerprint = body.at("fingerprint"), incarnation = body.at("boot");
			if (fingerprint != options.fingerprint || !valid_hex_token(incarnation))
			{
				error = "A fixed-roster peer has different verified initial content: " + notice.peer;
				return;
			}
			if (retired[notice.peer].contains(incarnation))
				return;
			if (!body.at("commit").is_number_unsigned())
				return;
			const bool peer_playing = body.at("playing"), peer_bootstrap = body.at("bootstrap");
			if (bootstrap && peer_playing)
			{
				broken = true;
				error = "Returning members require their original durable peer journal";
				return;
			}
			if (playing && peer_bootstrap)
				return;
			seen[notice.peer] = {incarnation, now, body.at("commit"), peer_playing};
		}
		else if (packet->type == "fleet-log" && !bootstrap)
			core->receive(notice.peer, body);
		else if (packet->type == "fleet-status")
		{
			if (core->leader() != notice.peer || body.at("term") != core->term() || !body.at("quorum").get<bool>())
				return;
			leader_status_peer = notice.peer;
			status_term = core->term();
			status_commit = body.at("commit");
			status_time = now;
		}
		else if (packet->type == "fleet-request" && leads() && playing)
		{
			const std::string incarnation = body.at("boot"), id = body.at("request");
			auto active = presence.find(notice.peer);
			if (!valid_hex_token(id) || active == presence.end() || active->second != incarnation ||
			    !body.at("value").is_object() || body.at("value").dump().size() > 44000)
				return;
			enqueue({notice.peer, incarnation, id, body.at("value")});
		}
	}
	void apply(CommittedEntry entry)
	{
		const auto &value = entry.value;
		const std::string kind = value.at("kind");
		if (kind == "consensus-snapshot")
		{
			const auto &snapshot = value.at("state");
			if (snapshot.at("fingerprint") != options.fingerprint)
				throw std::runtime_error("Checkpoint content differs from the fixed fleet");
			auto members = snapshot.at("presence").get<std::map<std::string, std::string>>();
			for (const auto &[peer, incarnation] : members)
				if (!is_member(peer) || !valid_hex_token(incarnation))
					throw std::runtime_error("Checkpoint changes the fixed roster");
			presence = std::move(members);
			retired = snapshot.at("retired").get<std::map<std::string, std::set<std::string>>>();
			completed = snapshot.at("completed").get<std::set<std::string>>();
			playing = snapshot.at("playing");
			barrier_term = snapshot.at("barrier_term");
			if (retired[options.roster.self].contains(boot))
				boot = random_token();
			requests.clear();
			own_requests.clear();
			inflight = false;
			inflight_id.clear();
			seen.clear();
			committed.push_back(
			    {entry.index, entry.term, {{"kind", "fleet-snapshot"}, {"state", snapshot.at("application")}}});
			return;
		}
		if (value.value("_driver", std::string{}) == inflight_id)
		{
			inflight = false;
			inflight_id.clear();
		}
		if (kind == "consensus-noop")
		{
			barrier_term = entry.term;
			return;
		}
		if (kind == "fleet-start")
		{
			if (playing)
				throw std::runtime_error("Repeated committed fleet start barrier");
			if (value.at("fingerprint") != options.fingerprint)
				throw std::runtime_error("Committed fleet start does not match verified initial content");
			auto members = value.at("members").get<std::map<std::string, std::string>>();
			if (members.size() != options.roster.members.size())
				throw std::runtime_error("Fleet start changed its fixed roster");
			for (const auto &id : options.roster.members)
				if (!members.contains(id) || !valid_hex_token(members.at(id)))
					throw std::runtime_error("Fleet start has an invalid member incarnation");
			presence = std::move(members);
			playing = true;
		}
		else if (kind == "fleet-presence")
		{
			const std::string peer = value.at("peer"), incarnation = value.at("boot");
			if (!playing || !is_member(peer) || (!incarnation.empty() && !valid_hex_token(incarnation)))
				throw std::runtime_error("Committed presence changed the fixed roster");
			const auto old = presence.find(peer);
			if (old != presence.end() && old->second != incarnation)
				retired[peer].insert(old->second);
			if (!incarnation.empty() && retired[peer].contains(incarnation))
				throw std::runtime_error("A retired peer incarnation cannot regain authority");
			if (incarnation.empty())
				presence.erase(peer);
			else
				presence[peer] = incarnation;
			const auto recent = seen.find(peer);
			if (recent != seen.end() && retired[peer].contains(recent->second.boot))
				seen.erase(recent);
			if (peer == options.roster.self && incarnation.empty())
			{
				boot = random_token();
				own_requests.clear();
				requests.clear();
				last_hello = 0;
			}
		}
		else if (kind == "fleet-frame")
		{
			const std::string origin = value.at("origin"), incarnation = value.at("boot"),
			                  request = value.at("request");
			if (!is_member(origin) || !valid_hex_token(request))
				throw std::runtime_error("Committed frame has an invalid origin or request");
			own_requests.erase(request);
			if (value.at("value").value("kind", std::string{}) != "tick" && !completed.insert(request).second)
				return;
			const auto member = presence.find(origin);
			// Old queued work cannot regain authority after absence or a process restart.
			if (member == presence.end() || member->second != incarnation)
				return;
			if (!value.at("value").is_object())
				throw std::runtime_error("Committed game frame must be an object");
		}
		else
			throw std::runtime_error("Unknown committed fleet frame");
		committed.push_back(std::move(entry));
	}
	bool append(WireJson value)
	{
		if (!leads() || inflight || core->pending_write())
			return false;
		const auto id = random_token();
		value["_driver"] = id;
		if (!core->propose(value))
			return false;
		inflight = true;
		inflight_id = id;
		return true;
	}
	void membership()
	{
		if (!leads() || inflight || core->pending_write())
			return;
		if (!playing)
		{
			WireJson members = WireJson::object();
			members[options.roster.self] = boot;
			for (const auto &peer : options.roster.members)
				if (peer != options.roster.self)
				{
					auto found = seen.find(peer);
					if (found == seen.end() || now - found->second.time > 1500)
						return;
					members[peer] = found->second.boot;
				}
			append({{"kind", "fleet-start"}, {"fingerprint", options.fingerprint}, {"members", members}});
			return;
		}
		for (const auto &peer : options.roster.members)
		{
			const auto active = presence.find(peer);
			const auto fresh = seen.find(peer);
			const bool here = peer == options.roster.self;
			const bool reachable = here || (fresh != seen.end() && now - fresh->second.time <= 1500);
			const std::string incarnation = here ? boot : reachable ? fresh->second.boot : std::string{};
			if (active != presence.end() && !reachable &&
			    (fresh == seen.end() ? now > 3000 : now - fresh->second.time > 3000))
			{
				append({{"kind", "fleet-presence"}, {"peer", peer}, {"boot", ""}});
				return;
			}
			// Presence becomes visible only through a barrier ordered after catch-up work.
			if (reachable && (active == presence.end() || active->second != incarnation) &&
			    (here || fresh->second.commit + 1 >= core->commit_index()))
			{
				append({{"kind", "fleet-presence"}, {"peer", peer}, {"boot", incarnation}});
				return;
			}
		}
	}
	void poll()
	{
		if (broken)
			return;
		const auto current = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - previous).count();
		previous += std::chrono::milliseconds(elapsed);
		now += std::max<std::int64_t>(0, elapsed);
		try
		{
			journal->poll();
			if (!core && journal->peer())
			{
				core = journal->peer();
				bootstrap = !journal->restored();
			}
			for (const auto &notice : transport->poll())
				if (core)
					try
					{
						handle(notice);
					}
					catch (const std::exception &)
					{ /* Malformed authenticated peer messages grant no operation. */
					}
			if (broken || !core)
				return;
			if (bootstrap)
			{
				bool all = true;
				for (const auto &peer : options.roster.members)
					if (peer != options.roster.self)
					{
						auto found = seen.find(peer);
						if (found == seen.end() || found->second.playing || now - found->second.time > 1500)
							all = false;
					}
				if (all)
					bootstrap = false;
			}
			if (active && !bootstrap)
				core->elapse(static_cast<unsigned>(std::min<std::int64_t>(1000, std::max<std::int64_t>(0, elapsed))));
			for (auto entry : core->take_committed())
				apply(std::move(entry));
			if (core->term() != observed_term)
			{
				observed_term = core->term();
				inflight = false;
				inflight_id.clear();
				requests.clear();
			}
			for (const auto &delivery : core->take_deliveries())
				send(delivery.peer, "fleet-log", delivery.packet);
			journal->flush();
			const bool available = authority();
			if (was_available && !available)
			{
				++admission_generation;
				own_requests.clear();
				requests.clear();
			}
			was_available = available;
			if (active && journal->durable() && now - last_hello >= 150)
			{
				for (const auto &peer : options.roster.members)
					if (peer != options.roster.self)
					{
						send(peer, "fleet-hello",
						     {{"fingerprint", options.fingerprint},
						      {"boot", boot},
						      {"commit", core->commit_index()},
						      {"playing", playing},
						      {"bootstrap", bootstrap}});
						if (leads())
							send(peer, "fleet-status",
							     {{"term", core->term()}, {"commit", core->commit_index()}, {"quorum", true}});
					}
				last_hello = now;
			}
			membership();
			if (ready() && now - last_retry >= 250)
			{
				for (const auto &[id, request] : own_requests)
				{
					if (leads())
						enqueue(request);
					else if (!core->leader().empty())
						send(core->leader(), "fleet-request",
						     {{"boot", boot}, {"request", id}, {"value", request.value}});
				}
				last_retry = now;
			}
			if (leads() && playing && !inflight && !core->pending_write())
				while (!requests.empty())
				{
					auto request = std::move(requests.front());
					requests.pop_front();
					const auto member = presence.find(request.origin);
					if (completed.contains(request.id) || member == presence.end() || member->second != request.boot)
						continue;
					if (append({{"kind", "fleet-frame"},
					            {"origin", request.origin},
					            {"boot", request.boot},
					            {"request", request.id},
					            {"value", request.value}}))
						break;
				}
		}
		catch (const std::exception &failure)
		{
			broken = true;
			error = failure.what();
			own_requests.clear();
			requests.clear();
		}
	}
};
FleetCommit::FleetCommit(FleetOptions options, std::unique_ptr<PeerTransport> transport)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(transport)))
{
}
FleetCommit::~FleetCommit() = default;
void FleetCommit::poll()
{
	impl_->poll();
}
bool FleetCommit::propose(const WireJson &value)
{
	auto &s = *impl_;
	if (!s.ready() || !value.is_object() || value.dump().size() > 44000 || s.own_requests.size() >= 32)
		return false;
	const auto id = random_token();
	Impl::Request request{s.options.roster.self, s.boot, id, value};
	s.own_requests.emplace(id, request);
	if (s.leads())
		s.enqueue(std::move(request));
	return true;
}
bool FleetCommit::propose_tick(const WireJson &value)
{
	auto &s = *impl_;
	if (!s.ready() || !s.leads() || !s.requests.empty() || !s.own_requests.empty() || !value.is_object() ||
	    value.dump().size() > 44000)
		return false;
	return s.append({{"kind", "fleet-frame"},
	                 {"origin", s.options.roster.self},
	                 {"boot", s.boot},
	                 {"request", random_token()},
	                 {"value", value}});
}
std::vector<CommittedEntry> FleetCommit::take_committed()
{
	auto result = std::move(impl_->committed);
	impl_->committed.clear();
	return result;
}
bool FleetCommit::checkpoint_due() const
{
	const auto &s = *impl_;
	return s.core && s.committed.empty() && s.core->checkpoint_due();
}
bool FleetCommit::checkpoint(const WireJson &state)
{
	auto &s = *impl_;
	return s.core && s.committed.empty() &&
	       s.core->checkpoint({{"fingerprint", s.options.fingerprint},
	                           {"presence", s.presence},
	                           {"retired", s.retired},
	                           {"completed", s.completed},
	                           {"playing", s.playing},
	                           {"barrier_term", s.barrier_term},
	                           {"application", state}});
}
bool FleetCommit::available() const
{
	return impl_->ready();
}
bool FleetCommit::leader() const
{
	return impl_->leads();
}
bool FleetCommit::started() const
{
	return impl_->playing;
}
bool FleetCommit::storage_ready() const
{
	return impl_->journal->durable() && !impl_->broken;
}
bool FleetCommit::activate()
{
	if (!storage_ready())
		return false;
	impl_->active = true;
	return true;
}
bool FleetCommit::failed() const
{
	return impl_->broken;
}
std::uint64_t FleetCommit::commit_index() const
{
	return impl_->core ? impl_->core->commit_index() : 0;
}
std::uint64_t FleetCommit::admission_generation() const
{
	return impl_->admission_generation;
}
std::string FleetCommit::status() const
{
	const auto &s = *impl_;
	if (s.broken)
		return "Fleet stopped: " + s.error;
	if (!s.error.empty())
		return s.error;
	if (!s.core)
		return "Opening durable peer log";
	if (!s.active)
		return "Waiting for durable server identity before fleet admission";
	if (!s.playing)
		return "Waiting for every fixed-roster peer to verify content and storage";
	return s.ready() ? s.leads() ? "Fleet leader / durable majority" : "Fleet follower / caught up"
	                 : "Fleet paused or catching up";
}
} // namespace phoenix
