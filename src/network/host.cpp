#include "../session/authority.hpp"
#include "projection.hpp"
#include <phoenix/consensus.hpp>
#include <phoenix/peer_store.hpp>
#include <phoenix/wire.hpp>
#include <chrono>
#include <deque>
#include <map>
namespace phoenix
{
namespace
{
class NetworkHost final : public SessionPort
{
	struct Client
	{
		std::string key, nonce, role;
		bool ready = false, approved = false, connected = true, renew = false;
		std::uint64_t seen = 0;
	};
	struct Input
	{
		std::string owner, lease;
		std::uint64_t sequence;
		OperatingCommand command;
	};
	NetworkOptions options_;
	std::shared_ptr<HostAccess> world_ = make_host_world();
	std::unique_ptr<PeerTransport> transport_;
	std::unique_ptr<PeerStore> store_;
	std::unique_ptr<PeerConsensus> consensus_;
	std::unique_ptr<PackSource> source_;
	std::map<std::string, Client> clients_;
	std::deque<Input> inputs_;
	std::deque<Json> intents_;
	std::string epoch_ = random_token(), offer_ = random_token(), message_, owner_, lease_, proposal_;
	std::uint64_t console_ = 0, next_console_ = 2, last_sequence_ = 0, now_ = 0, last_projection_ = 0;
	bool writing_ = false, want_local_ = false, fault_ = false;
	std::chrono::steady_clock::time_point last_poll_ = std::chrono::steady_clock::now();
	static constexpr const char *local = "local-console";
	EntityId ship() const
	{
		if (!world_->running())
			return 0;
		return world_->authoritative_world().ship_for_slot(world_->setup().local_slot).value_or(0);
	}
	bool available() const
	{
		return consensus_ && consensus_->has_quorum() && !fault_;
	}
	void send(const std::string &peer, const std::string &type, const Json &body, bool unsigned_packet = false)
	{
		auto found = clients_.find(peer);
		if (!unsigned_packet && found == clients_.end())
			return;
		try
		{
			transport_->send(peer,
			                 encode_packet({unsigned_packet ? "lobby" : epoch_, options_.link.self, peer, type, body},
			                               unsigned_packet ? "" : found->second.key));
		}
		catch (const std::exception &error)
		{
			message_ = error.what();
		}
	}
	void advertise(const std::string &peer)
	{
		if (source_)
			send(peer, "pack-offer", {{"offer", offer_}, {"content", source_->offer()}});
	}
	void refresh_source()
	{
		source_.reset();
		if (!world_->content_archives().empty())
			source_ = std::make_unique<PackSource>(world_->content_archives());
		offer_ = random_token();
		for (auto &[id, client] : clients_)
		{
			client.ready = false;
			advertise(id);
		}
	}
	bool propose(Json value)
	{
		if (!available() || !proposal_.empty())
			return false;
		value["proposal"] = random_token();
		if (!consensus_->propose(value))
			return false;
		proposal_ = value.at("proposal");
		return true;
	}
	bool intent(Json value)
	{
		if (!available() || intents_.size() >= 32)
			return false;
		for (const auto &pending : intents_)
			if (pending == value || (value.at("kind") == "station-claim" && pending.at("kind") == "station-claim" &&
			                         pending.at("owner") == value.at("owner")))
				return true;
		intents_.push_back(std::move(value));
		return true;
	}
	void claim(const std::string &peer, bool replace)
	{
		const bool local_claim = peer == local;
		auto found = clients_.find(peer);
		if (!local_claim && (found == clients_.end() || !found->second.ready || found->second.role != "client"))
			return;
		auto refused = [&](const std::string &reason, bool confirm = false)
		{
			if (local_claim)
				message_ = reason;
			else
				send(peer, "claim-result", {{"ok", false}, {"reason", reason}, {"confirm", confirm}});
		};
		if (!world_->running() || !available())
		{
			refused("The ship server is waiting for a durable majority");
			return;
		}
		if (console_)
		{
			if (owner_ == peer)
			{
				if (!local_claim)
					send(peer, "claim-result", {{"ok", true}, {"lease", lease_}});
				return;
			}
			refused("This station is already occupied");
			return;
		}
		if (!owner_.empty() && owner_ != peer && !replace)
		{
			refused("Confirm replacing the disconnected operator", true);
			return;
		}
		Json value{{"kind", "station-claim"},
		           {"owner", peer},
		           {"console", local_claim ? 1 : next_console_},
		           {"lease", random_token()}};
		if (!local_claim)
			value["key"] = found->second.key;
		if (!intent(value))
			refused("Station request queue is unavailable; try again");
	}
	void begin_log()
	{
		consensus_.reset();
		store_.reset();
		writing_ = false;
		proposal_.clear();
		fault_ = false;
		store_ = open_peer_store(options_.data_location + "/peers/" + options_.profile + "-" + epoch_ + ".journal");
		message_ = "Opening durable ship-server log";
	}
	void hello(const std::string &sender, const std::string &bytes)
	{
		if (sender == local || sender == options_.link.self)
			return;
		auto found = clients_.find(sender);
		auto packet = decode_packet(bytes, sender, options_.link.self, "lobby", "");
		if (packet && packet->type == "hello" && found != clients_.end())
		{
			auto proof = decode_packet(packet->body.value("resume", std::string{}), sender, options_.link.self, "lobby",
			                           found->second.key);
			const bool unfinished =
			    !found->second.ready && packet->body.value("nonce", std::string{}) == found->second.nonce;
			if (!unfinished &&
			    (!proof || proof->type != "hello" || proof->body.at("nonce") != packet->body.at("nonce") ||
			     proof->body.at("role") != packet->body.at("role")))
				packet.reset();
		}
		if (!packet || packet->type != "hello")
		{
			send(sender, "refused",
			     {{"reason", "This identity needs its stored credential; use a new profile if starting again"}}, true);
			return;
		}
		const std::string role = packet->body.at("role"), nonce = packet->body.at("nonce");
		if ((role != "client" && role != "gm") || !network::token(nonce))
			return;
		if (found == clients_.end())
		{
			if (clients_.size() >= 32)
			{
				send(sender, "refused", {{"reason", "Ship-server connection capacity reached"}}, true);
				return;
			}
			found = clients_.emplace(sender, Client{random_token(), nonce, role}).first;
		}
		else if (found->second.role != role)
			return;
		auto &client = found->second;
		if (client.nonce != nonce && sender == owner_ && console_)
		{
			client.renew = true;
			inputs_.clear();
		}
		client.nonce = nonce;
		client.connected = true;
		client.seen = now_;
		send(sender, "welcome", {{"key", client.key}, {"epoch", epoch_}, {"role", role}, {"nonce", nonce}}, true);
		advertise(sender);
	}
	void packet(const std::string &sender, const std::string &bytes)
	{
		auto found = clients_.find(sender);
		if (found == clients_.end())
		{
			hello(sender, bytes);
			return;
		}
		auto decoded = decode_packet(bytes, sender, options_.link.self, epoch_, found->second.key);
		if (!decoded)
		{
			hello(sender, bytes);
			return;
		}
		auto &client = found->second;
		client.connected = true;
		client.seen = now_;
		const auto &body = decoded->body;
		if (decoded->type == "offer-request")
			advertise(sender);
		else if (decoded->type == "pack-request" && source_ && body.at("offer") == offer_)
		{
			auto chunk = source_->chunk(body);
			chunk["offer"] = offer_;
			send(sender, "pack-chunk", chunk);
		}
		else if (decoded->type == "ready")
		{
			if (source_ && body.at("offer") == offer_ && body.at("packs") == Json(world_->catalog().ordered_packs))
			{
				client.ready = true;
				message_ = "Verified client content: " + sender;
			}
		}
		else if (decoded->type == "claim")
			claim(sender, body.value("replace", false));
		else if (decoded->type == "release" && sender == owner_ && console_)
			intent({{"kind", "station-release"}, {"owner", owner_}, {"lease", lease_}, {"forget", true}});
		else if (decoded->type == "command")
		{
			const auto sequence = network::counter(body.at("sequence"));
			if (!body.at("value").is_number_integer())
				throw std::runtime_error("Operating command value must be an integer");
			OperatingCommand command{network::counter(body.at("target")), body.at("action"),
			                         body.at("value").get<std::int64_t>()};
			if (client.role != "client" || client.renew || sender != owner_ || !console_ ||
			    body.at("lease") != lease_ || sequence <= last_sequence_ || !available() || inputs_.size() >= 128 ||
			    !world_->authoritative_world().can_submit(console_, command))
			{
				send(sender, "command-result",
				     {{"ok", false},
				      {"reason", "Command refused: claim, lease, sequence, quorum or fitted capability mismatch"}});
				return;
			}
			last_sequence_ = sequence;
			inputs_.push_back({sender, lease_, sequence, std::move(command)});
		}
		else if (decoded->type != "heartbeat" && decoded->type != "pack-request")
			send(sender, "refused", {{"reason", "Unsupported operation for this connection"}});
	}
	void apply(const CommittedEntry &entry)
	{
		const auto &value = entry.value;
		const std::string kind = value.at("kind");
		if (kind == "consensus-noop")
			return;
		if (value.value("proposal", std::string{}) == proposal_)
			proposal_.clear();
		auto &simulation = world_->authoritative_world();
		if (kind == "station-claim")
		{
			if (console_)
				throw std::runtime_error("Committed station claim conflicts with an active operator");
			const auto id = network::counter(value.at("console"));
			if (!simulation.claim(id, ship()))
				throw std::runtime_error("Committed station admission failed");
			owner_ = value.at("owner");
			lease_ = value.at("lease");
			console_ = id;
			next_console_ = std::max(next_console_, id + 1);
			last_sequence_ = 0;
			inputs_.clear();
			if (owner_ != local)
				send(owner_, "claim-result", {{"ok", true}, {"lease", lease_}});
		}
		else if (kind == "station-release")
		{
			if (value.at("owner") == owner_ && value.at("lease") == lease_)
			{
				if (console_)
					simulation.release(console_);
				console_ = 0;
				inputs_.clear();
				auto client = clients_.find(owner_);
				if (client != clients_.end())
					client->second.renew = false;
				if (value.value("forget", false))
				{
					owner_.clear();
					lease_.clear();
				}
			}
		}
		else if (kind == "gm-grant")
		{
			const std::string peer = value.at("owner");
			auto found = clients_.find(peer);
			if (found != clients_.end() && found->second.role == "gm")
				found->second.approved = true;
		}
		else if (kind == "tick")
		{
			if (network::counter(value.at("tick")) != simulation.ticks() + 1)
				throw std::runtime_error("Committed simulation tick is not contiguous");
			for (const auto &input : value.at("inputs"))
			{
				if (input.at("owner") != owner_ || input.at("lease") != lease_ || !console_)
					throw std::runtime_error("Committed input refers to a stale station claim");
				OperatingCommand command{network::counter(input.at("target")), input.at("action"), input.at("value")};
				if (!simulation.submit(console_, command))
					throw std::runtime_error("Committed operating command failed production admission");
			}
			world_->tick();
		}
		else
			throw std::runtime_error("Unknown committed game frame");
	}
	void project()
	{
		if (now_ - last_projection_ < 100)
			return;
		last_projection_ = now_;
		for (auto &[id, client] : clients_)
		{
			if (!client.connected || !client.ready)
				continue;
			Json state{{"started", world_->running()},
			           {"tick", ticks()},
			           {"station", client.renew     ? "reconnecting"
			                       : console_       ? "occupied"
			                       : owner_.empty() ? "empty"
			                                        : "disconnected"},
			           {"owner", id == owner_},
			           {"approved", client.approved},
			           {"quorum", available()}};
			if (world_->running() && client.role == "client" && !client.renew && console_ && id == owner_)
			{
				auto view = world_->authoritative_world().observe(console_);
				if (view)
				{
					state["observation"] = network::observation_json(*view);
					state["lease"] = lease_;
				}
			}
			if (world_->running() && client.role == "gm" && client.approved)
				state["inspection"] = world_->authoritative_world().save();
			send(id, "state", state);
		}
	}

public:
	explicit NetworkHost(NetworkOptions options) : options_(std::move(options))
	{
		if (options_.link.self.empty())
			options_.link.self = random_token();
		if (options_.link.room.empty())
			options_.link.room = random_token();
		if (options_.link.url.empty())
			options_.link.url = "ws://127.0.0.1:9080/phoenix";
		if (options_.profile.empty() ||
		    options_.profile.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") !=
		        std::string::npos)
			throw std::runtime_error("Invalid local server profile");
		transport_ = open_peer_transport(options_.link);
	}
	void add_pack(const Bytes &archive) override
	{
		world_->add_pack(archive);
		refresh_source();
	}
	void clear_packs() override
	{
		for (const auto &[id, client] : clients_)
			send(id, "refused", {{"reason", "Session ended; join again"}}, true);
		consensus_.reset();
		store_.reset();
		world_->clear_packs();
		source_.reset();
		clients_.clear();
		inputs_.clear();
		intents_.clear();
		owner_.clear();
		lease_.clear();
		console_ = 0;
		epoch_ = random_token();
		proposal_.clear();
		fault_ = false;
	}
	const Catalog &catalog() const override
	{
		return world_->catalog();
	}
	void start(const GameSetup &setup) override
	{
		if (world_->running() || world_->recovery_pending())
			throw std::runtime_error("End the current session before starting another");
		world_->start(setup);
		world_->authoritative_world().clear_claims();
		owner_.clear();
		console_ = 0;
		inputs_.clear();
		refresh_source();
		begin_log();
	}
	bool running() const override
	{
		return world_->running();
	}
	bool recovery_pending() const override
	{
		return world_->recovery_pending();
	}
	Bytes save() const override
	{
		return world_->save();
	}
	void restore(const Bytes &archive) override
	{
		auto candidate = make_host_world();
		candidate->restore(archive);
		clear_packs();
		world_ = std::move(candidate);
		refresh_source();
	}
	void resume_local() override
	{
		world_->resume_local();
		world_->authoritative_world().clear_claims();
		begin_log();
		want_local_ = true;
	}
	void tick() override
	{
		if (!running() || !available() || !proposal_.empty() || !intents_.empty())
			return;
		Json commands = Json::array();
		if (consensus_->pending_write())
			return;
		while (!inputs_.empty() && commands.size() < 64)
		{
			auto input = std::move(inputs_.front());
			inputs_.pop_front();
			if (input.owner != owner_ || input.lease != lease_ || !console_ ||
			    !world_->authoritative_world().can_submit(console_, input.command))
				continue;
			commands.push_back({{"owner", input.owner},
			                    {"lease", input.lease},
			                    {"sequence", input.sequence},
			                    {"target", input.command.target},
			                    {"action", input.command.action},
			                    {"value", input.command.value}});
		}
		propose({{"kind", "tick"}, {"tick", ticks() + 1}, {"inputs", commands}});
	}
	std::uint64_t ticks() const override
	{
		return world_->ticks();
	}
	bool command(const OperatingCommand &command) override
	{
		if (owner_ != local || !console_ || !available() || inputs_.size() >= 128 ||
		    !world_->authoritative_world().can_submit(console_, command))
			return false;
		inputs_.push_back({local, lease_, ++last_sequence_, command});
		return true;
	}
	std::optional<Observation> observation() const override
	{
		return running() && owner_ == local && console_ ? world_->authoritative_world().observe(console_)
		                                                : std::nullopt;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return world_->viewscreen();
	}
	std::optional<BodyView> focus_body() const override
	{
		if (!running())
			return {};
		for (const auto &body : world_->authoritative_world().inspect())
			if (body.id == ship())
				return body;
		return {};
	}
	Json inspection() const override
	{
		return running() ? world_->authoritative_world().save() : Json(nullptr);
	}
	bool networked() const override
	{
		return true;
	}
	std::string invitation_code() const override
	{
		return options_.link.room + "/" + options_.link.self;
	}
	std::string connection_status() const override
	{
		return "Ship server / code " + invitation_code() + " / " +
		       (fault_        ? "stopped"
		        : available() ? "durable majority ready"
		                      : "waiting for durable consensus") +
		       ". " + message_;
	}
	std::vector<std::pair<std::string, std::string>> lobby_controls() const override
	{
		std::vector<std::pair<std::string, std::string>> controls;
		if (running())
		{
			if (!console_)
				controls.emplace_back("network.local", owner_.empty()
				                                           ? "Claim local ship console"
				                                           : "Replace disconnected operator with local console");
			if (console_ && owner_ == local)
				controls.emplace_back("network.release", "Release local ship console");
			for (const auto &[id, client] : clients_)
				if (client.connected && client.ready && client.role == "gm" && !client.approved)
					controls.emplace_back("network.gm/" + id, "Approve read-only GM: " + id);
		}
		return controls;
	}
	bool session_action(const std::string &action) override
	{
		if (action == "network.local")
		{
			claim(local, !owner_.empty());
			return true;
		}
		if (action == "network.release" && owner_ == local && console_)
			return intent({{"kind", "station-release"}, {"owner", owner_}, {"lease", lease_}, {"forget", true}});
		if (action.rfind("network.gm/", 0) == 0)
		{
			auto id = action.substr(11);
			auto client = clients_.find(id);
			if (client != clients_.end() && client->second.ready && client->second.role == "gm")
				return intent({{"kind", "gm-grant"}, {"owner", id}});
		}
		return false;
	}
	void poll() override
	{
		auto current = std::chrono::steady_clock::now();
		auto wall_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - last_poll_).count();
		auto elapsed = static_cast<unsigned>(std::min<std::int64_t>(1000, wall_elapsed));
		last_poll_ = current;
		now_ += wall_elapsed;
		try
		{
			for (const auto &event : transport_->poll())
			{
				if (event.kind == LinkEvent::packet)
					try
					{
						packet(event.peer, event.data);
					}
					catch (const std::exception &error)
					{
						send(event.peer, "refused", {{"reason", error.what()}});
					}
				if (event.kind == LinkEvent::error)
					message_ = event.data;
			}
			if (store_)
				if (auto event = store_->poll())
				{
					if (!event->error.empty())
					{
						if (consensus_ && !event->opened)
							consensus_->persisted(event->generation, false);
						throw std::runtime_error(event->error);
					}
					if (event->opened)
					{
						if (!event->restored.is_null())
							throw std::runtime_error("Unexpected existing log for a new session epoch");
						consensus_ = std::make_unique<PeerConsensus>(
						    PeerRoster{epoch_, options_.link.self, {options_.link.self}});
					}
					else if (consensus_)
					{
						consensus_->persisted(event->generation, true);
						writing_ = false;
					}
				}
			if (consensus_)
			{
				consensus_->elapse(elapsed);
				for (const auto &entry : consensus_->take_committed())
					apply(entry);
				if (consensus_->role() == PeerRole::failed)
					throw std::runtime_error("Peer consensus stopped after storage failure");
				if (!writing_)
					if (auto write = consensus_->pending_write())
						writing_ = store_->write(*write);
			}
			if (available() && proposal_.empty() && !consensus_->pending_write())
				while (!intents_.empty())
				{
					auto value = std::move(intents_.front());
					intents_.pop_front();
					const std::string kind = value.at("kind"), owner = value.at("owner");
					if (kind == "station-claim")
					{
						if (console_)
						{
							if (owner != local)
								send(owner, "claim-result",
								     {{"ok", false}, {"reason", "This station is already occupied"}});
							continue;
						}
						if (owner != local)
						{
							auto client = clients_.find(owner);
							if (client == clients_.end() || now_ - client->second.seen > 3000 || !client->second.ready)
								continue;
						}
					}
					if (kind == "station-release" && (owner != owner_ || value.at("lease") != lease_ || !console_))
						continue;
					if (propose(value))
						break;
				}
			if (want_local_ && available() && proposal_.empty())
			{
				claim(local, false);
				if (!intents_.empty())
					want_local_ = false;
			}
			if (console_ && owner_ != local)
			{
				auto client = clients_.find(owner_);
				if (client != clients_.end() && client->second.renew)
					propose({{"kind", "station-release"}, {"owner", owner_}, {"lease", lease_}, {"forget", false}});
			}
			for (auto &[id, client] : clients_)
				if (now_ - client.seen > 3000)
					client.connected = false;
			if (console_ && owner_ != local)
			{
				auto client = clients_.find(owner_);
				if (client == clients_.end() || now_ - client->second.seen > 3000)
				{
					if (client != clients_.end())
						client->second.connected = false;
					propose({{"kind", "station-release"}, {"owner", owner_}, {"lease", lease_}, {"forget", false}});
				}
			}
			project();
		}
		catch (const std::exception &error)
		{
			fault_ = true;
			message_ = error.what();
		}
	}
};
} // namespace
std::shared_ptr<SessionPort> make_network_host(NetworkOptions options)
{
	return std::make_shared<NetworkHost>(std::move(options));
}
} // namespace phoenix
