module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.network.lobby;
import phoenix.network.host;
import phoenix.network.client;
import phoenix.session.local;
import phoenix.fleet_setup;
import phoenix.wire;
namespace phoenix
{
namespace
{
std::string runtime_identity(const std::string &peer)
{
	const auto text = "crew/" + peer;
	return sha256(Bytes(text.begin(), text.end()));
}
class GameLobby final : public SessionPort
{
	struct Peer
	{
		LobbyMember member;
		std::string key, nonce;
		std::uint64_t seen = 0, command_sequence = 0;
	};
	NetworkOptions options_;
	bool coordinator_ = true, offline_ = false, active_ = false, launched_ = false, welcomed_ = false;
	bool recovery_ = false, setup_ack_ = false;
	JoinRole role_ = JoinRole::ship_server;
	std::string self_, target_, epoch_ = random_token(), nonce_ = random_token(), key_, error_;
	std::shared_ptr<HostAccess> source_ = make_host_world();
	std::shared_ptr<SessionPort> backend_;
	std::unique_ptr<PeerTransport> transport_;
	std::map<std::string, Peer> peers_;
	GameSetup selection_;
	std::vector<std::string> recovery_slots_;
	Bytes bundle_;
	Json received_ = Json::object(), configuration_ = Json::object();
	std::unique_ptr<StateSource> setup_source_;
	std::unique_ptr<StateTransfer> setup_transfer_;
	std::unique_ptr<PackSource> packs_source_;
	std::unique_ptr<PackTransfer> packs_transfer_;
	std::uint64_t last_setup_ack_ = 0;
	std::uint64_t revision_ = 0, now_ = 0, last_broadcast_ = 0, last_hello_ = 0, sequence_ = 0, generation_ = 0;
	std::optional<std::uint64_t> countdown_;
	std::chrono::steady_clock::time_point previous_ = std::chrono::steady_clock::now();
	bool local_claimed_ = false;
	void send(const std::string &peer, const std::string &type, const Json &body, bool plain = false)
	{
		if (!transport_)
			return;
		const auto &key = plain ? std::string{} : coordinator_ ? peers_.at(peer).key : key_;
		transport_->send(peer, encode_packet({plain ? "lobby" : epoch_, self_, peer, type, body}, key));
	}
	void hello()
	{
		Json body{{"role", role_ == JoinRole::gm ? "gm" : "client"},
		          {"join_role", static_cast<int>(role_)},
		          {"nonce", nonce_},
		          {"protocol", "pregame-1"}};
		if (!key_.empty())
			body["resume"] = encode_packet({"lobby", self_, target_, "hello", body}, key_);
		send(target_, "hello", body, true);
		last_hello_ = now_;
	}
	std::vector<LobbySlot> slots() const
	{
		std::vector<LobbySlot> result;
		if (!source_->catalog().maps.contains(selection_.mission))
			return result;
		for (const auto &slot : source_->catalog().maps.at(selection_.mission).at("ship_slots"))
		{
			LobbySlot value;
			value.id = slot.at("id");
			value.mandatory = slot.value("mandatory", false);
			value.allowed_ships = slot.at("allowed_ships").get<std::vector<std::string>>();
			if (recovery_)
			{
				if (!selection_.ships.contains(value.id))
					continue;
				value.ship = selection_.ships.at(value.id);
				value.allowed_ships = {value.ship};
			}
			for (const auto &[id, peer] : peers_)
				if (peer.member.role == JoinRole::ship_server && peer.member.slot == value.id)
				{
					value.host = id;
					if (!recovery_)
						value.ship = selection_.ships.at(value.id);
				}
			if (value.ship.empty() && value.mandatory)
				value.ship = value.allowed_ships.front();
			result.push_back(std::move(value));
		}
		return result;
	}
	Json snapshot() const
	{
		Json data{
		    {"revision", revision_},
		    {"scenario", selection_.mission},
		    {"launched", launched_},
		    {"countdown", countdown_ ? static_cast<int>(5000 - std::min<std::uint64_t>(5000, now_ - *countdown_)) : -1},
		    {"slots", Json::array()},
		    {"members", Json::array()},
		    {"consoles", Json::object()}};
		for (const auto &slot : slots())
		{
			data["slots"].push_back({{"id", slot.id},
			                         {"ship", slot.ship},
			                         {"host", slot.host},
			                         {"mandatory", slot.mandatory},
			                         {"allowed", slot.allowed_ships}});
			if (!slot.ship.empty())
				for (const auto &seat : resolve_consoles(source_->catalog(), slot.ship))
					data["consoles"][slot.id].push_back(seat.id);
		}
		for (const auto &[id, peer] : peers_)
		{
			const auto &m = peer.member;
			data["members"].push_back({{"id", id},
			                           {"name", m.name},
			                           {"slot", m.slot},
			                           {"console", m.console},
			                           {"role", static_cast<int>(m.role)},
			                           {"approved", m.approved},
			                           {"connected", m.connected},
			                           {"ready", m.ready},
			                           {"verified", m.verified}});
		}
		return data;
	}
	void invalidate()
	{
		countdown_.reset();
		++revision_;
		++generation_;
		setup_ack_ = false;
		for (auto &[id, p] : peers_)
		{
			p.member.ready = false;
			p.member.verified = p.member.role != JoinRole::ship_server;
		}
		setup_source_.reset();
		bundle_.clear();
		if (selection_.local_slot.empty())
			return;
		std::map<std::string, std::string> members;
		for (const auto &[id, p] : peers_)
			if (p.member.role == JoinRole::ship_server && !p.member.slot.empty())
				members[id] = p.member.slot;
		if (recovery_)
		{
			Json plan{{"room", options_.link.room}, {"members", members}, {"creator", self_}};
			bundle_ = save_fleet_recovery(*source_, plan);
		}
		else
			bundle_ = export_fleet_setup(*source_, selection_, options_.link.room, members);
		setup_source_ = std::make_unique<StateSource>(bundle_, revision_);
		peers_.at(self_).member.verified = true;
	}
	bool all_ready() const
	{
		if (!setup_source_ || launched_)
			return false;
		for (const auto &slot : recovery_slots_)
			if (std::none_of(peers_.begin(), peers_.end(),
			                 [&](const auto &p)
			                 {
				                 return p.second.member.role == JoinRole::ship_server && p.second.member.slot == slot;
			                 }))
				return false;
		for (const auto &[id, p] : peers_)
		{
			const auto &m = p.member;
			if (!m.approved || m.role == JoinRole::gm)
				continue;
			if (!m.connected || !m.verified || m.slot.empty())
				return false;
			if (m.role == JoinRole::player)
			{
				if (m.console.empty() || !m.ready)
					return false;
			}
			else
			{
				const bool crew = std::any_of(peers_.begin(), peers_.end(),
				                              [&](const auto &v)
				                              {
					                              return v.second.member.approved && v.second.member.connected &&
					                                     v.second.member.role == JoinRole::player &&
					                                     v.second.member.slot == m.slot;
				                              });
				if ((!crew || !m.console.empty()) && !m.ready)
					return false;
			}
		}
		return true;
	}
	bool command(const std::string &id, const LobbyCommand &cmd)
	{
		if (cmd.action == LobbyAction::leave && id == self_ && !cmd.member.empty() && cmd.member != self_)
		{
			if (!peers_.contains(cmd.member) || launched_)
				return false;
			const auto removed = peers_.at(cmd.member).member;
			send(cmd.member, "removed", {{"reason", "Removed from lobby by host"}});
			peers_.erase(cmd.member);
			if (!recovery_ && removed.role == JoinRole::ship_server)
				selection_.ships.erase(removed.slot);
			invalidate();
			return true;
		}
		auto &member = peers_.at(id).member;
		if (cmd.action == LobbyAction::approve)
		{
			if (id != self_ || !peers_.contains(cmd.member))
				return false;
			auto &other = peers_.at(cmd.member).member;
			if (launched_ && other.role == JoinRole::ship_server)
				return false;
			other.approved = true;
			if (!launched_)
				invalidate();
			return true;
		}
		if (!member.approved || !member.connected)
			return false;
		if (cmd.action == LobbyAction::claim_ship)
		{
			if (launched_ || member.role != JoinRole::ship_server)
				return false;
			const auto available = slots();
			auto slot = std::find_if(available.begin(), available.end(),
			                         [&](const auto &s)
			                         {
				                         return s.id == cmd.slot;
			                         });
			if (slot == available.end() || (!slot->host.empty() && slot->host != id) ||
			    std::find(slot->allowed_ships.begin(), slot->allowed_ships.end(), cmd.ship) ==
			        slot->allowed_ships.end())
				return false;
			if (recovery_ &&
			    std::find(recovery_slots_.begin(), recovery_slots_.end(), cmd.slot) == recovery_slots_.end())
				return false;
			if (!member.slot.empty() && !recovery_)
				selection_.ships.erase(member.slot);
			const auto old_slot = member.slot;
			member.slot = cmd.slot;
			member.console.clear();
			selection_.ships[cmd.slot] = cmd.ship;
			if (id == self_)
				selection_.local_slot = cmd.slot;
			for (auto &[peer_id, p] : peers_)
				if (peer_id != id && p.member.slot == old_slot && p.member.role != JoinRole::ship_server)
				{
					p.member.slot.clear();
					p.member.console.clear();
				}
			invalidate();
			return true;
		}
		if (cmd.action == LobbyAction::select_console)
		{
			if (member.role == JoinRole::gm)
			{
				member.slot = cmd.slot;
				return true;
			}
			const auto available = slots();
			auto slot = std::find_if(available.begin(), available.end(),
			                         [&](const auto &s)
			                         {
				                         return s.id == cmd.slot && !s.host.empty();
			                         });
			if (slot == available.end() || (member.role == JoinRole::ship_server && member.slot != cmd.slot))
				return false;
			const auto seats = resolve_consoles(source_->catalog(), slot->ship);
			if (!cmd.console.empty() && std::none_of(seats.begin(), seats.end(),
			                                         [&](const auto &seat)
			                                         {
				                                         return seat.id == cmd.console;
			                                         }))
				return false;
			if (!cmd.console.empty() && std::any_of(peers_.begin(), peers_.end(),
			                                        [&](const auto &p)
			                                        {
				                                        return p.first != id && p.second.member.slot == cmd.slot &&
				                                               p.second.member.console == cmd.console;
			                                        }))
				return false;
			member.slot = cmd.slot;
			member.console = cmd.console;
			if (!launched_)
				invalidate();
			return true;
		}
		if (cmd.action == LobbyAction::ready || cmd.action == LobbyAction::unready)
		{
			if (launched_ || member.role == JoinRole::gm || !member.verified || member.slot.empty() ||
			    (member.role == JoinRole::player && member.console.empty()))
				return false;
			member.ready = cmd.action == LobbyAction::ready;
			countdown_.reset();
			return true;
		}
		if (cmd.action == LobbyAction::leave && id != self_)
		{
			member.connected = false;
			member.ready = false;
			if (!launched_)
			{
				if (!recovery_ && member.role == JoinRole::ship_server)
					selection_.ships.erase(member.slot);
				peers_.erase(id);
				invalidate();
			}
			return true;
		}
		return false;
	}
	Json config_for(const std::string &id) const
	{
		const auto &member = peers_.at(id).member;
		Json result{
		    {"revision", revision_}, {"slot", member.slot}, {"console", member.console}, {"tickets", Json::object()}};
		if (member.role == JoinRole::ship_server)
			for (const auto &[peer_id, p] : peers_)
				if (p.member.approved && p.member.role != JoinRole::ship_server && p.member.slot == member.slot)
					result["tickets"][runtime_identity(peer_id)] = {{"key", p.key},
					                                                {"role", static_cast<int>(p.member.role)}};
		return result;
	}
	std::string endpoint(const std::string &slot) const
	{
		const auto data = coordinator_ ? snapshot() : received_;
		for (const auto &s : data.value("slots", Json::array()))
			if (s.at("id") == slot && !s.at("host").get<std::string>().empty())
				return s.at("host").get<std::string>() + "-console";
		return {};
	}
	void launch_local()
	{
		if (backend_)
			return;
		const auto config = coordinator_ ? config_for(self_) : configuration_;
		if (role_ == JoinRole::ship_server)
		{
			auto prepared = read_fleet_setup(bundle_, self_);
			if (offline_)
				backend_ = prepared.world;
			else
			{
				auto options = options_;
				options.link.self = self_;
				for (const auto &[id, ticket] : config.at("tickets").items())
					options.lobby_tickets[id] = {ticket.at("key"), static_cast<JoinRole>(ticket.at("role").get<int>())};
				backend_ = make_network_host(options);
				backend_->restore(bundle_);
				backend_->activate_prepared();
			}
		}
		else
		{
			const std::string slot = config.at("slot");
			const auto server = endpoint(slot);
			if (server.empty())
				return;
			auto options = options_;
			options.server = server;
			options.link.self = runtime_identity(self_);
			options.inspector = role_ == JoinRole::gm;
			options.admission = key_;
			options.profile += "-" + epoch_ + "-" + runtime_identity(self_);
			backend_ = make_network_client(options);
		}
		local_claimed_ = config.at("console").get<std::string>().empty();
		++generation_;
	}
	void broadcast()
	{
		const auto state = snapshot();
		for (const auto &[id, p] : peers_)
			if (id != self_ && p.member.connected)
			{
				send(id, "lobby-state", state);
				if (!p.member.approved)
					continue;
				send(id, "configuration", config_for(id));
				if (p.member.role == JoinRole::ship_server && packs_source_)
				{
					send(id, "pack-offer", {{"content", packs_source_->offer()}});
					if (setup_source_ && !p.member.slot.empty())
						send(id, "setup-offer", setup_source_->offer());
				}
				if (launched_ && (p.member.role != JoinRole::ship_server || p.member.verified) &&
				    !p.member.slot.empty())
					send(id, "launch", {{"revision", revision_}});
			}
	}
	void host_packet(const std::string &sender, const std::string &bytes)
	{
		auto found = peers_.find(sender);
		auto packet = found == peers_.end() ? std::optional<GamePacket>{}
		                                    : decode_packet(bytes, sender, self_, epoch_, found->second.key);
		if (!packet)
		{
			auto hello = decode_packet(bytes, sender, self_, "lobby", "");
			if (!hello || hello->type != "hello" || hello->body.value("protocol", std::string{}) != "pregame-1")
				return;
			const auto role = hello->body.at("join_role").get<int>();
			const std::string nonce = hello->body.at("nonce");
			if (role < 0 || role > 2 || !valid_hex_token(nonce) || sender == self_)
				return;
			if (found == peers_.end())
			{
				if (peers_.size() >= 48 || (launched_ && role == static_cast<int>(JoinRole::ship_server)))
				{
					send(sender, "refused", {{"reason", "This lobby cannot admit another ship server"}}, true);
					return;
				}
				Peer peer;
				peer.member.id = sender;
				peer.member.role = static_cast<JoinRole>(role);
				peer.member.name = std::string(role == 0   ? "Ship Host "
				                               : role == 1 ? "Player "
				                                           : "GM ") +
				                   std::to_string(peers_.size());
				peer.key = random_token();
				peer.nonce = nonce;
				peer.seen = now_;
				found = peers_.emplace(sender, std::move(peer)).first;
			}
			else if (found->second.nonce != nonce || static_cast<int>(found->second.member.role) != role)
				return;
			send(sender, "welcome",
			     {{"key", found->second.key}, {"epoch", epoch_}, {"nonce", nonce}, {"protocol", "pregame-1"}}, true);
			return;
		}
		auto &peer = found->second;
		if (!peer.member.connected)
		{
			peer.member.connected = true;
			peer.member.ready = false;
			countdown_.reset();
		}
		peer.seen = now_;
		if (packet->type == "heartbeat")
			return;
		if (!peer.member.approved)
			return;
		if (packet->type == "lobby-command")
		{
			const auto seq = packet->body.at("sequence").get<std::uint64_t>();
			if (seq <= peer.command_sequence)
				return;
			peer.command_sequence = seq;
			const auto &b = packet->body;
			if (!command(sender, {static_cast<LobbyAction>(b.at("action").get<int>()), b.value("member", std::string{}),
			                      b.value("slot", std::string{}), b.value("ship", std::string{}),
			                      b.value("console", std::string{})}))
				send(sender, "lobby-error", {{"reason", "Lobby selection changed or is unavailable"}});
		}
		else if (packet->type == "pack-request" && peer.member.role == JoinRole::ship_server && packs_source_)
			send(sender, "pack-chunk", packs_source_->chunk(packet->body));
		else if (packet->type == "setup-request" && peer.member.role == JoinRole::ship_server &&
		         !peer.member.slot.empty() && setup_source_ &&
		         packet->body.at("sequence") == setup_source_->offer().at("sequence"))
			send(sender, "setup-chunk", setup_source_->chunk(packet->body));
		else if (packet->type == "setup-verified" && peer.member.role == JoinRole::ship_server &&
		         packet->body.at("revision") == revision_ && setup_source_ &&
		         packet->body.at("hash") == setup_source_->offer().at("hash"))
			peer.member.verified = true;
	}
	void client_packet(const std::string &bytes)
	{
		if (auto welcome = decode_packet(bytes, target_, self_, "lobby", ""))
		{
			if (welcome->type == "welcome" && welcome->body.at("nonce") == nonce_ &&
			    welcome->body.value("protocol", std::string{}).empty())
			{
				if (role_ == JoinRole::ship_server)
				{
					error_ = "New ships must join a pre-game lobby, before launch";
					return;
				}
				transport_.reset();
				auto options = options_;
				options.link.self.clear();
				options.inspector = role_ == JoinRole::gm;
				backend_ = make_network_client(options);
				launched_ = true;
				return;
			}
			if (welcome->type == "welcome" && welcome->body.at("nonce") == nonce_ &&
			    welcome->body.value("protocol", std::string{}) == "pregame-1")
			{
				const std::string key = welcome->body.at("key"), epoch = welcome->body.at("epoch");
				if (!valid_hex_token(key) || !valid_hex_token(epoch))
					return;
				key_ = key;
				epoch_ = epoch;
				welcomed_ = true;
			}
			else if (welcome->type == "refused")
			{
				if (welcome->body.value("protocol", std::string{}) == "direct" && role_ != JoinRole::ship_server)
				{
					transport_.reset();
					auto options = options_;
					options.link.self.clear();
					options.inspector = role_ == JoinRole::gm;
					backend_ = make_network_client(options);
					launched_ = true;
				}
				else
					error_ = welcome->body.value("protocol", std::string{}) == "direct"
					             ? "New ships must join a pre-game lobby, before launch"
					             : welcome->body.value("reason", std::string{});
			}
			return;
		}
		if (!welcomed_)
			return;
		auto packet = decode_packet(bytes, target_, self_, epoch_, key_);
		if (!packet)
			return;
		const auto &body = packet->body;
		if (packet->type == "lobby-state")
		{
			if (!body.at("slots").is_array() || body.at("slots").size() > 16 || !body.at("members").is_array() ||
			    body.at("members").size() > 48)
				throw std::runtime_error("Lobby exceeds supported bounds");
			if (body.at("revision").get<std::uint64_t>() >= revision_)
			{
				received_ = body;
				revision_ = body.at("revision");
			}
		}
		else if (packet->type == "configuration")
			configuration_ = body;
		else if (packet->type == "pack-offer" && role_ == JoinRole::ship_server && !packs_transfer_)
			packs_transfer_ = std::make_unique<PackTransfer>(body.at("content"));
		else if (packet->type == "pack-chunk" && packs_transfer_ && !packs_transfer_->ready())
		{
			packs_transfer_->accept(body);
			if (packs_transfer_->ready())
				for (const auto &pack : packs_transfer_->packs())
					source_->add_verified_pack(pack);
		}
		else if (packet->type == "setup-offer" && role_ == JoinRole::ship_server)
		{
			if (!setup_transfer_ || body.at("sequence").get<std::uint64_t>() > setup_transfer_->sequence())
			{
				setup_transfer_ = std::make_unique<StateTransfer>(body, true);
				setup_ack_ = false;
			}
		}
		else if (packet->type == "setup-chunk" && setup_transfer_ && !setup_transfer_->ready())
			setup_transfer_->accept(body);
		else if (packet->type == "removed")
		{
			active_ = false;
			error_ = body.at("reason");
			transport_.reset();
		}
		else if (packet->type == "lobby-error")
			error_ = body.at("reason");
		else if (packet->type == "launch" && configuration_.value("revision", std::uint64_t{0}) == body.at("revision"))
		{
			if (role_ == JoinRole::ship_server && (!setup_ack_ || setup_transfer_->sequence() != body.at("revision")))
				return;
			launched_ = true;
			launch_local();
		}
	}

public:
	GameLobby(NetworkOptions options, bool coordinator, JoinRole role, bool offline = false)
	    : options_(std::move(options)), coordinator_(coordinator), offline_(offline), role_(role)
	{
		if (options_.profile.empty() ||
		    options_.profile.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") !=
		        std::string::npos)
			throw std::runtime_error("Invalid local server profile");
		if (options_.link.room.empty())
			options_.link.room = random_token().substr(0, 10);
		if (options_.link.self.empty())
			options_.link.self = coordinator_ ? "host" : random_token();
		self_ = options_.link.self;
		target_ = options_.server;
		if (coordinator_)
		{
			Peer own;
			own.member.id = self_;
			own.member.name = "Host";
			own.member.role = role_;
			own.member.approved = true;
			own.member.verified = true;
			own.key = random_token();
			peers_[self_] = own;
		}
		else
		{
			active_ = true;
			if (target_.empty())
				throw std::runtime_error("Enter a lobby join code");
		}
		if (!offline_)
			transport_ = open_peer_transport(options_.link);
	}
	void add_pack(const Bytes &archive) override
	{
		if (active_)
			throw std::runtime_error("Leave the lobby before changing packs");
		source_->add_pack(archive);
	}
	void replace_packs(const std::vector<Bytes> &archives) override
	{
		if (active_)
			throw std::runtime_error("Leave the lobby before changing packs");
		source_->replace_packs(archives);
		++generation_;
	}
	void clear_packs() override
	{
		backend_.reset();
		active_ = launched_ = recovery_ = false;
		countdown_.reset();
		source_->clear_packs();
		selection_ = {};
		recovery_slots_.clear();
		setup_source_.reset();
		bundle_.clear();
		std::erase_if(peers_,
		              [&](const auto &p)
		              {
			              return p.first != self_;
		              });
		if (coordinator_)
		{
			peers_.at(self_).member.slot.clear();
			peers_.at(self_).member.console.clear();
			peers_.at(self_).member.ready = false;
		}
		++generation_;
	}
	const Catalog &catalog() const override
	{
		return backend_ ? backend_->catalog() : source_->catalog();
	}
	void start(const GameSetup &setup) override
	{
		// Existing explicit launch arguments retain their direct-start contract.
		backend_ = offline_ ? make_local_session() : make_network_host(options_);
		for (const auto &pack : source_->content_packs())
			backend_->add_pack(pack.archive());
		backend_->start(setup);
		launched_ = true;
	}
	void prepare_lobby(const GameSetup &setup) override
	{
		if (!coordinator_ || active_)
			throw std::runtime_error("Leave the current lobby first");
		if (!source_->catalog().maps.contains(setup.mission) || !setup.ships.contains(setup.local_slot))
			throw std::runtime_error("Choose a scenario, slot and allowed ship");
		selection_ = setup;
		selection_.local_console.clear();
		peers_.at(self_).member.slot = setup.local_slot;
		packs_source_ = std::make_unique<PackSource>(source_->content_packs());
		invalidate();
		active_ = true;
	}
	void restore(const Bytes &archive) override
	{
		if (!coordinator_)
			throw std::runtime_error("Only a host can restore a save");
		auto pack = read_pack(archive);
		auto candidate = make_host_world();
		std::vector<std::string> required;
		if (pack.manifest().value("format", std::string{}) == "phoenix-fleet-setup")
		{
			auto prepared = read_fleet_setup(archive);
			candidate = prepared.world;
			for (const auto &[id, slot] : prepared.plan.at("members").items())
				required.push_back(slot);
		}
		else
		{
			candidate->restore(archive);
			candidate->resume_server();
			required.push_back(candidate->setup().local_slot);
		}
		auto setup = candidate->setup();
		backend_.reset();
		source_ = std::move(candidate);
		selection_ = std::move(setup);
		recovery_slots_ = std::move(required);
		recovery_ = true;
		active_ = true;
		launched_ = false;
		std::erase_if(peers_,
		              [&](const auto &p)
		              {
			              return p.first != self_;
		              });
		peers_.at(self_).member.slot = selection_.local_slot;
		peers_.at(self_).member.console.clear();
		packs_source_ = std::make_unique<PackSource>(source_->content_packs());
		invalidate();
	}
	bool running() const override
	{
		return backend_ && backend_->running();
	}
	bool recovery_pending() const override
	{
		return active_ && recovery_ && !launched_;
	}
	Bytes save() const override
	{
		if (!backend_)
			throw std::runtime_error("Start the game before saving");
		return backend_->save();
	}
	Bytes fleet_setup(const GameSetup &setup) const override
	{
		return export_fleet_setup(*source_, setup, options_.link.room);
	}
	void resume_local() override
	{
		if (backend_)
			backend_->resume_local();
		else
			lobby_command({LobbyAction::ready});
	}
	void tick() override
	{
		if (backend_)
			backend_->tick();
	}
	std::uint64_t ticks() const override
	{
		return backend_ ? backend_->ticks() : recovery_ ? source_->ticks() : 0;
	}
	std::uint64_t generation() const override
	{
		return generation_ + (backend_ ? backend_->generation() : source_->generation());
	}
	bool command(const OperatingCommand &value) override
	{
		return backend_ && backend_->command(value);
	}
	std::optional<Observation> observation() const override
	{
		return backend_ ? backend_->observation() : std::nullopt;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return backend_ ? backend_->viewscreen() : std::vector<BodyView>{};
	}
	std::vector<SceneBody> scene() const override
	{
		return backend_ ? backend_->scene() : std::vector<SceneBody>{};
	}
	Json scene_lighting() const override
	{
		return backend_ ? backend_->scene_lighting() : Json::object();
	}
	std::vector<ConsoleSeat> console_seats() const override
	{
		return backend_ ? backend_->console_seats() : std::vector<ConsoleSeat>{};
	}
	bool claim_console(std::uint64_t route, const ConsoleAddress &seat) override
	{
		return backend_ && backend_->claim_console(route, seat);
	}
	bool release_console(std::uint64_t route) override
	{
		return backend_ && backend_->release_console(route);
	}
	bool console_command(std::uint64_t route, const OperatingCommand &v) override
	{
		return backend_ && backend_->console_command(route, v);
	}
	std::optional<Observation> console_observation(std::uint64_t route) const override
	{
		return backend_ ? backend_->console_observation(route) : std::nullopt;
	}
	Json inspection() const override
	{
		return backend_ ? backend_->inspection() : Json{};
	}
	std::optional<BodyView> focus_body() const override
	{
		return backend_ ? backend_->focus_body() : std::nullopt;
	}
	bool networked() const override
	{
		return !offline_;
	}
	std::string invitation_code() const override
	{
		return backend_ ? backend_->invitation_code() : options_.link.room + "/" + (coordinator_ ? self_ : target_);
	}
	std::string connection_status() const override
	{
		return backend_ ? backend_->connection_status() : error_.empty() ? "Lobby / " + invitation_code() : error_;
	}
	LobbyProjection lobby() const override
	{
		LobbyProjection result;
		result.active = active_;
		result.coordinator = coordinator_;
		result.launched = launched_;
		result.self = self_;
		result.code = options_.link.room + "/" + (coordinator_ ? self_ : target_);
		result.status = error_;
		const auto data = coordinator_ ? snapshot() : received_;
		result.scenario = data.value("scenario", std::string{});
		result.countdown_ms = data.value("countdown", -1);
		for (const auto &s : data.value("slots", Json::array()))
			result.slots.push_back({s.at("id"), s.at("ship"), s.at("host"), s.at("mandatory"),
			                        s.at("allowed").get<std::vector<std::string>>()});
		for (const auto &m : data.value("members", Json::array()))
			result.members.push_back({m.at("id"), m.at("slot"), m.at("console"),
			                          static_cast<JoinRole>(m.at("role").get<int>()), m.at("approved"),
			                          m.at("connected"), m.at("ready"), m.at("verified"),
			                          m.value("name", std::string("Player"))});
		return result;
	}
	bool lobby_command(const LobbyCommand &cmd) override
	{
		if (coordinator_)
			return command(self_, cmd);
		if (!welcomed_)
			return false;
		send(target_, "lobby-command",
		     {{"sequence", ++sequence_},
		      {"action", static_cast<int>(cmd.action)},
		      {"member", cmd.member},
		      {"slot", cmd.slot},
		      {"ship", cmd.ship},
		      {"console", cmd.console}});
		return true;
	}
	std::vector<std::pair<std::string, std::string>> lobby_controls() const override
	{
		if (backend_)
			return backend_->lobby_controls();
		std::vector<std::pair<std::string, std::string>> result;
		const auto view = lobby();
		for (const auto &m : view.members)
		{
			if (view.coordinator && !m.approved && m.connected)
				result.emplace_back("lobby.approve/" + m.id, "Approve " + m.name);
			if (view.coordinator && m.id != self_ && !launched_)
				result.emplace_back("lobby.remove/" + m.id, "Remove " + m.name);
			if (m.id != self_ || !m.approved)
				continue;
			if (!m.slot.empty() && m.role != JoinRole::gm && m.verified)
			{
				bool crew =
				    std::any_of(view.members.begin(), view.members.end(),
				                [&](const auto &p)
				                {
					                return p.approved && p.connected && p.role == JoinRole::player && p.slot == m.slot;
				                });
				if (m.role == JoinRole::player || !m.console.empty() || !crew)
					result.emplace_back(m.ready ? "lobby.unready" : "lobby.ready", m.ready ? "Not Ready" : "Ready");
			}
			for (const auto &slot : view.slots)
			{
				if (m.role == JoinRole::ship_server && (slot.host.empty() || slot.host == self_))
					for (const auto &ship : slot.allowed_ships)
						result.emplace_back("lobby.ship/" + slot.id + "/" + ship, slot.id + ": " + ship);
				if (!slot.host.empty() && (m.role != JoinRole::ship_server || m.slot == slot.id))
				{
					if (m.role == JoinRole::gm)
						result.emplace_back("lobby.seat/" + slot.id + "/", "Inspect " + slot.id);
					else
					{
						const auto data = coordinator_ ? snapshot() : received_;
						if (data.at("consoles").contains(slot.id))
							for (const auto &seat : data.at("consoles").at(slot.id))
							{
								const std::string console = seat;
								if (std::none_of(view.members.begin(), view.members.end(),
								                 [&](const auto &p)
								                 {
									                 return p.id != self_ && p.slot == slot.id && p.console == console;
								                 }))
									result.emplace_back("lobby.seat/" + slot.id + "/" + console,
									                    "Console: " + slot.id + " / " + console);
							}
						if (m.role == JoinRole::ship_server && !m.console.empty())
							result.emplace_back("lobby.seat/" + slot.id + "/", "Use AI crew");
					}
				}
			}
		}
		return result;
	}
	bool session_action(const std::string &id) override
	{
		if (backend_)
			return backend_->session_action(id);
		if (id == "lobby.ready" || id == "lobby.unready")
			return lobby_command({id == "lobby.ready" ? LobbyAction::ready : LobbyAction::unready});
		if (id.starts_with("lobby.approve/"))
			return lobby_command({LobbyAction::approve, id.substr(14)});
		if (id.starts_with("lobby.remove/"))
			return lobby_command({LobbyAction::leave, id.substr(13)});
		if (id.starts_with("lobby.ship/") || id.starts_with("lobby.seat/"))
		{
			const std::size_t begin = 11;
			const auto split = id.find('/', begin);
			if (split == std::string::npos)
				return false;
			LobbyCommand cmd{id.starts_with("lobby.ship/") ? LobbyAction::claim_ship : LobbyAction::select_console};
			cmd.slot = id.substr(begin, split - begin);
			if (cmd.action == LobbyAction::claim_ship)
				cmd.ship = id.substr(split + 1);
			else
				cmd.console = id.substr(split + 1);
			return lobby_command(cmd);
		}
		return false;
	}
	void poll() override
	{
		const auto current = std::chrono::steady_clock::now();
		now_ += std::chrono::duration_cast<std::chrono::milliseconds>(current - previous_).count();
		previous_ = current;
		try
		{
			if (transport_)
			{
				for (const auto &event : transport_->poll())
				{
					if (event.kind == LinkEvent::packet)
					{
						try
						{
							if (coordinator_)
								host_packet(event.peer, event.data);
							else if (event.peer == target_)
								client_packet(event.data);
						}
						catch (const std::exception &e)
						{
							error_ = e.what();
						}
					}
					else if (event.kind == LinkEvent::error)
						error_ = event.data;
				}
			}
			if (!coordinator_)
			{
				if (!welcomed_ && now_ - last_hello_ >= 500)
					hello();
				if (welcomed_)
				{
					if (now_ - last_broadcast_ >= 500)
					{
						send(target_, "heartbeat", Json::object());
						last_broadcast_ = now_;
					}
					if (packs_transfer_ && !packs_transfer_->ready())
						for (const auto &r : packs_transfer_->requests(now_))
							send(target_, "pack-request", r);
					if (setup_transfer_ && !setup_transfer_->ready())
						for (const auto &r : setup_transfer_->requests(now_))
							send(target_, "setup-request", r);
					if (setup_transfer_ && setup_transfer_->ready() && !setup_ack_ &&
					    configuration_.value("revision", std::uint64_t{0}) == setup_transfer_->sequence())
					{
						read_fleet_setup(setup_transfer_->archive(), self_);
						bundle_ = setup_transfer_->archive();
						setup_ack_ = true;
					}
					if (setup_ack_ && !backend_ && now_ - last_setup_ack_ >= 500)
					{
						send(target_, "setup-verified",
						     {{"revision", setup_transfer_->sequence()}, {"hash", sha256(bundle_)}});
						last_setup_ack_ = now_;
					}
				}
			}
			else if (active_)
			{
				for (auto &[id, peer] : peers_)
					if (id != self_ && peer.member.connected && now_ - peer.seen > 4000)
					{
						peer.member.connected = false;
						peer.member.ready = false;
						countdown_.reset();
					}
				if (!launched_)
				{
					if (all_ready())
					{
						if (!countdown_)
							countdown_ = now_;
						if (now_ - *countdown_ >= 5000)
						{
							launched_ = true;
							launch_local();
						}
					}
					else
						countdown_.reset();
				}
				if (now_ - last_broadcast_ >= 200)
				{
					broadcast();
					last_broadcast_ = now_;
				}
			}
			if (backend_)
			{
				const auto active_config = coordinator_ ? config_for(self_) : configuration_;
				if (role_ == JoinRole::ship_server && active_config.contains("tickets"))
					for (const auto &[id, ticket] : active_config.at("tickets").items())
						backend_->authorise_lobby_member(
						    id, {ticket.at("key"), static_cast<JoinRole>(ticket.at("role").get<int>())});
				backend_->poll();
				if (!local_claimed_)
				{
					const auto config = coordinator_ ? config_for(self_) : configuration_;
					const std::string console = config.at("console");
					for (const auto &seat : backend_->console_seats())
						if (seat.address.console == console && seat.available)
							if (backend_->claim_console(1, seat.address))
								break;
					local_claimed_ = backend_->observation().has_value();
				}
			}
		}
		catch (const std::exception &e)
		{
			error_ = e.what();
			countdown_.reset();
		}
	}
};
} // namespace
std::shared_ptr<SessionPort> make_game_lobby(NetworkOptions options, bool offline)
{
	return std::make_shared<GameLobby>(std::move(options), true, JoinRole::ship_server, offline);
}
std::shared_ptr<SessionPort> join_game_lobby(NetworkOptions options, JoinRole role)
{
	return std::make_shared<GameLobby>(std::move(options), false, role);
}
} // namespace phoenix
