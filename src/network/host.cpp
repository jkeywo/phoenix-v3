module;
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.network.host;
import phoenix.persistence;
import phoenix.replication;
import phoenix.session.api;
import phoenix.session.local;
import phoenix.session.execution;
import phoenix.fleet_setup;
import phoenix.wire;
#include "projection.hpp"

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
		std::shared_ptr<StateSource> projection;
		std::uint64_t projection_sequence = 0, projection_started = 0;
		std::string projection_stream = random_token();
	};
	NetworkOptions options_;
	std::string member_;
	std::shared_ptr<HostAccess> world_ = make_host_world();
	std::unique_ptr<GameExecution> game_;
	std::unique_ptr<PeerTransport> transport_;
	std::unique_ptr<DurableConsensus> journal_;
	PeerConsensus *consensus_ = nullptr;
	std::unique_ptr<FleetRuntime> fleet_;
	std::unique_ptr<PackSource> source_;
	std::unique_ptr<PackSource> console_source_;
	std::map<std::string, Client> clients_;
	Json grants_ = Json::object();
	std::deque<Json> intents_;
	std::map<std::uint64_t, std::uint64_t> local_sequences_;
	std::string epoch_ = random_token(), offer_ = random_token(), message_, proposal_;
	std::uint64_t generation_base_ = 0, now_ = 0, last_projection_ = 0, term_ = 0;
	std::uint64_t fleet_generation_ = 0;
	bool want_local_ = false, fault_ = false;
	bool tick_due_ = false, last_proposal_was_tick_ = true;
	std::chrono::steady_clock::time_point last_poll_ = std::chrono::steady_clock::now();
	static constexpr const char *local = "local-console";
	bool available() const
	{
		return !fault_ && (fleet_ ? fleet_->available() : consensus_ && consensus_->has_quorum());
	}
	void send(const std::string &peer, const std::string &type, const Json &body, bool unsigned_packet = false)
	{
		auto found = clients_.find(peer);
		if (!transport_ || (!unsigned_packet && found == clients_.end()))
			return;
		try
		{
			auto payload = body;
			if (!unsigned_packet && type.starts_with("state-"))
				payload["stream"] = found->second.projection_stream;
			transport_->send(
			    peer, encode_packet({unsigned_packet ? "lobby" : epoch_, options_.link.self, peer, type, payload},
			                        unsigned_packet ? "" : found->second.key));
		}
		catch (const std::exception &error)
		{
			message_ = error.what();
		}
	}
	void advertise(const std::string &peer)
	{
		auto &client = clients_.at(peer);
		auto *source = client.role == "gm" ? source_.get() : console_source_.get();
		if (source && client.approved)
			send(peer, "pack-offer", {{"offer", offer_}, {"content", source->offer()}});
	}
	void refresh_source()
	{
		source_.reset();
		console_source_.reset();
		if (!world_->content_packs().empty())
			source_ = std::make_unique<PackSource>(world_->content_packs());
		if (world_->running() && !world_->setup().gm_host)
			console_source_ = std::make_unique<PackSource>(std::vector<Pack>{
			    console_assets(world_->catalog(), world_->setup().ships.at(world_->setup().local_slot))});
		offer_ = random_token();
		for (auto &[id, client] : clients_)
		{
			client.ready = false;
			advertise(id);
		}
	}
	bool propose(Json value, bool tick = false)
	{
		if (!available() || !proposal_.empty())
			return false;
		value["proposal"] = random_token();
		const bool accepted =
		    fleet_ ? (tick ? fleet_->propose_tick(value) : fleet_->propose(value)) : consensus_->propose(value);
		if (!accepted)
			return false;
		proposal_ = value.at("proposal");
		return true;
	}
	bool intent(Json value)
	{
		if (!available() || intents_.size() >= 128)
			return false;
		for (const auto &pending : intents_)
			if (pending == value)
				return true;
		intents_.push_back(std::move(value));
		return true;
	}
	ConsoleAddress default_seat() const
	{
		if (!game_)
			return {};
		auto seats = game_->seats(member_);
		for (const auto &seat : seats)
			if (seat.address.console == world_->setup().local_console)
				return seat.address;
		return seats.size() == 1 ? seats.front().address : ConsoleAddress{};
	}
	bool claim(const std::string &peer, std::uint64_t route, ConsoleAddress address, bool replace)
	{
		if (peer != local)
		{
			auto found = clients_.find(peer);
			if (found == clients_.end() || !found->second.approved || !found->second.ready ||
			    found->second.role != "client")
				return false;
		}
		if (!address.ship && game_)
			for (const auto &seat : game_->seats(member_))
			{
				const auto *lease = game_->lease(seat.address);
				if (lease->owner == peer && lease->route == route)
				{
					address = seat.address;
					break;
				}
			}
		if (!address.ship)
			address = default_seat();
		if (!game_ || !available())
			return false;
		if (auto existing = game_->owned(member_, peer, route))
		{
			if (peer != local)
				send(peer, "claim-result", {{"ok", true}, {"lease", existing->token}});
			return existing->address == address;
		}
		if (!game_->can_claim(member_, peer, route, address, replace))
		{
			auto seat = game_->lease(address);
			const bool confirm = seat && !seat->active && !seat->owner.empty() && seat->owner != peer;
			message_ = confirm ? "Confirm replacing the disconnected operator"
			                   : "Choose an available Console on this ship server";
			if (peer != local)
				send(peer, "claim-result", {{"ok", false}, {"reason", message_}, {"confirm", confirm}});
			return false;
		}
		return intent({{"kind", "station-claim"},
		               {"owner", peer},
		               {"route", route},
		               {"ship", address.ship},
		               {"seat", address.console},
		               {"lease", random_token()},
		               {"replace", replace}});
	}
	bool release(const std::string &peer, std::uint64_t route, bool forget)
	{
		auto seat = game_ ? game_->owned(member_, peer, route) : nullptr;
		return seat && intent({{"kind", "station-release"},
		                       {"owner", peer},
		                       {"route", route},
		                       {"lease", seat->token},
		                       {"forget", forget}});
	}
	void begin_log()
	{
		consensus_ = nullptr;
		journal_.reset();
		proposal_.clear();
		fault_ = false;
		game_ = std::make_unique<GameExecution>(
		    world_, std::map<std::string, std::string>{{member_, world_->setup().local_slot}});
		journal_ = std::make_unique<DurableConsensus>(
		    PeerRoster{epoch_, member_, {member_}, options_.checkpoint_frames},
		    options_.data_location + "/peers/" + options_.profile + "-" + epoch_ + ".journal", JournalRecovery::fresh);
		message_ = "Opening durable ship-server log";
	}
	void hello(const std::string &sender, const std::string &bytes)
	{
		// Recover durable grants before responding to a returning credential holder.
		// A welcome issued during replay could overwrite that client's stored key.
		if (fleet_ && !available())
			return;
		if (sender == local || sender == options_.link.self)
			return;
		auto found = clients_.find(sender);
		auto packet = decode_packet(bytes, sender, options_.link.self, "lobby", "");
		if (packet && packet->type == "hello" && packet->body.value("protocol", std::string{}).starts_with("pregame-"))
		{
			send(sender, "refused", {{"protocol", "direct"}, {"reason", "Use direct ship-server admission"}}, true);
			return;
		}
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
		if ((role != "client" && role != "gm") || !valid_hex_token(nonce))
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
		if (client.nonce != nonce && game_ && game_->owned(member_, sender))
			client.renew = true;
		client.nonce = nonce;
		client.connected = true;
		client.seen = now_;
		if (!client.approved && options_.lobby_tickets.contains(sender))
		{
			auto admission = decode_packet(packet->body.value("admission", std::string{}), sender, options_.link.self,
			                               "lobby", options_.lobby_tickets.at(sender).key);
			const auto authorised_role = options_.lobby_tickets.at(sender).role == JoinRole::gm ? "gm" : "client";
			if (options_.lobby_tickets.at(sender).role != JoinRole::ship_server && role == authorised_role &&
			    admission && admission->type == "hello" && admission->body.value("nonce", std::string{}) == nonce &&
			    admission->body.value("role", std::string{}) == role)
				intent({{"kind", "client-grant"},
				        {"owner", sender},
				        {"key", client.key},
				        {"nonce", nonce},
				        {"role", role}});
		}
		send(sender, "welcome",
		     {{"key", client.key},
		      {"epoch", epoch_},
		      {"role", role},
		      {"nonce", nonce},
		      {"stream", client.projection_stream}},
		     true);
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
		if (!client.approved)
		{
			send(sender, "refused", {{"reason", "Waiting for host approval"}});
			return;
		}
		const auto &body = decoded->body;
		if (decoded->type.starts_with("state-") && body.value("stream", std::string{}) != client.projection_stream)
			return;
		auto *source = client.role == "gm" ? source_.get() : console_source_.get();
		if (decoded->type == "state-request" && client.ready && client.projection)
		{
			if (body.at("sequence") == client.projection->offer().at("sequence"))
				send(sender, "state-chunk", client.projection->chunk(body));
		}
		else if (decoded->type == "state-ack" && client.projection)
		{
			if (body.at("sequence") == client.projection->offer().at("sequence"))
				client.projection.reset();
		}
		else if (decoded->type == "offer-request")
			advertise(sender);
		else if (decoded->type == "pack-request" && source && body.at("offer") == offer_)
		{
			auto chunk = source->chunk(body);
			chunk["offer"] = offer_;
			send(sender, "pack-chunk", chunk);
		}
		else if (decoded->type == "ready")
		{
			if (source)
			{
				std::vector<std::pair<std::string, std::string>> identities;
				const auto offer = source->offer();
				for (const auto &pack : offer.at("packs"))
					identities.emplace_back(pack.at("id"), pack.at("hash"));
				if (body.at("offer") == offer_ && body.at("packs") == Json(identities))
					client.ready = true;
			}
		}
		else if (decoded->type == "claim")
			claim(sender, 1, {body.value("ship", EntityId{0}), body.value("seat", std::string{})},
			      body.value("replace", false));
		else if (decoded->type == "release")
			release(sender, 1, true);
		else if (decoded->type == "command")
		{
			if (!body.at("value").is_number_integer())
				throw std::runtime_error("Operating command value must be an integer");
			const auto command = decode_operating_command(body);
			const auto sequence = network::counter(body.at("sequence"));
			const std::string token = body.at("lease");
			if (client.role != "client" || client.renew || !game_ ||
			    !game_->can_command(member_, sender, 1, token, sequence, command) ||
			    !intent({{"kind", "command"},
			             {"owner", sender},
			             {"route", 1},
			             {"lease", token},
			             {"sequence", sequence},
			             {"target", command.target},
			             {"action", command.action},
			             {"value", command.value},
			             {"system", command.system},
			             {"controller", command.controller},
			             {"channel", command.channel},
			             {"generation", command.generation},
			             {"request_id", command.request_id},
			             {"arguments", command.arguments},
			             {"console", command.console}}))
				send(sender, "command-result",
				     {{"ok", false},
				      {"reason", "Command refused: Console lease, sequence, quorum or System permission mismatch"}});
		}
	}
	void apply_frame(const std::string &origin, const Json &value)
	{
		if (origin == member_ && value.value("proposal", std::string{}) == proposal_)
			proposal_.clear();
		const std::string kind = value.at("kind");
		if (kind == "consensus-noop")
			return;
		if (kind == "client-grant")
		{
			grants_[origin][value.at("owner").get<std::string>()] = value;
			if (origin != member_)
				return;
			const std::string peer = value.at("owner");
			auto found = clients_.find(peer);
			if (found == clients_.end())
				found =
				    clients_
				        .emplace(peer, Client{value.at("key"), value.at("nonce"), value.at("role"), false, true, false})
				        .first;
			if (found->second.key != value.at("key").get<std::string>() ||
			    found->second.role != value.at("role").get<std::string>())
				return;
			found->second.approved = true;
			if (found->second.connected)
				advertise(peer);
			return;
		}
		const bool accepted = game_->apply(origin, value);
		if (origin != member_)
			return;
		if (kind == "station-claim")
		{
			const std::string owner = value.at("owner");
			if (owner != local)
				send(owner, "claim-result",
				     accepted ? Json{{"ok", true}, {"lease", value.at("lease")}}
				              : Json{{"ok", false}, {"reason", "Console changed before admission; choose it again"}});
		}
		if (kind == "station-release")
		{
			auto found = clients_.find(value.at("owner").get<std::string>());
			if (found != clients_.end())
				found->second.renew = false;
		}
	}
	void apply(const CommittedEntry &entry)
	{
		const auto &value = entry.value;
		if (value.at("kind") == "consensus-snapshot" || value.at("kind") == "fleet-snapshot")
		{
			const auto &state = value.at("state");
			world_->authoritative_world().restore(world_->catalog(), state.at("world"));
			game_->restore(state.at("leases"));
			grants_ = state.at("grants");
			if (grants_.contains(member_))
				for (const auto &[id, grant] : grants_.at(member_).items())
				{
					auto found = clients_.find(id);
					if (found == clients_.end())
						clients_.emplace(
						    id, Client{grant.at("key"), grant.at("nonce"), grant.at("role"), false, true, false});
					else if (found->second.key == grant.at("key").get<std::string>())
						found->second.approved = true;
				}
			proposal_.clear();
			intents_.clear();
			local_sequences_.clear();
			++generation_base_;
			return;
		}
		if (!fleet_)
		{
			apply_frame(member_, value);
			return;
		}
		const std::string kind = value.at("kind");
		if (kind == "fleet-frame")
			apply_frame(value.at("origin"), value.at("value"));
		else if (kind == "fleet-presence")
		{
			const std::string peer = value.at("peer");
			game_->absent(peer);
			if (peer == member_)
			{
				proposal_.clear();
				intents_.clear();
				local_sequences_.clear();
			}
		}
	}
	void project()
	{
		if (now_ - last_projection_ < 100)
			return;
		last_projection_ = now_;
		for (auto &[id, client] : clients_)
		{
			if (!client.connected || !client.approved || !client.ready)
				continue;
			if (client.projection && now_ - client.projection_started < 15000)
			{
				send(id, "state-offer", client.projection->offer());
				continue;
			}
			auto held = game_ ? game_->owned(member_, id) : nullptr;
			Json seats = Json::array();
			bool previous_owner = false;
			if (game_)
				for (const auto &seat : game_->seats(member_))
				{
					const auto *lease = game_->lease(seat.address);
					previous_owner |= lease->owner == id;
					seats.push_back({{"ship", seat.address.ship},
					                 {"seat", seat.address.console},
					                 {"station", seat.spec.station},
					                 {"definition", seat.spec.definition},
					                 {"controls", seat.spec.controls},
					                 {"feeds", seat.spec.feeds},
					                 {"available", seat.available},
					                 {"replace", !lease->active && !lease->owner.empty() && lease->owner != id}});
				}
			Json state{{"started", running()},
			           {"tick", ticks()},
			           {"station", held             ? "occupied"
			                       : previous_owner ? "disconnected"
			                                        : "empty"},
			           {"owner", previous_owner},
			           {"approved", true},
			           {"quorum", available()},
			           {"seats", seats}};
			if (running() && client.role == "client" && !client.renew)
				state["public_board"] = network::public_board_json(world_->public_board());
			if (running() && client.role == "client" && !client.renew && held)
				if (auto view = game_->observe(member_, id))
				{
					state["observation"] = network::observation_json(*view);
					state["lease"] = held->token;
				}
			if (running() && client.role == "gm")
				try
				{
					state["inspection"] = world_->authoritative_world().save();
				}
				catch (const std::exception &)
				{
					continue;
				}
			try
			{
				client.projection = std::make_shared<StateSource>(state, ++client.projection_sequence);
				client.projection_started = now_;
				send(id, "state-offer", client.projection->offer());
			}
			catch (const std::exception &error)
			{
				message_ = error.what();
				send(id, "refused", {{"reason", message_}});
			}
		}
	}
	void install_fleet(PreparedFleet prepared)
	{
		clear_packs();
		generation_base_ += world_->generation();
		world_ = prepared.world;
		member_ = prepared.self;
		epoch_ = prepared.plan.at("session");
		options_.link.room = prepared.plan.at("room");
		auto peer_options = options_;
		peer_options.link.self = member_;
		// Fleet replication and crew connections own distinct transport endpoints.
		options_.link.self = member_ + "-console";
		transport_.reset();
		transport_ = open_peer_transport(options_.link);
		game_ = std::make_unique<GameExecution>(world_,
		                                        prepared.plan.at("members").get<std::map<std::string, std::string>>());
		fleet_ = std::make_unique<FleetRuntime>(std::move(prepared), peer_options);
		refresh_source();
	}

public:
	explicit NetworkHost(NetworkOptions options) : options_(std::move(options))
	{
		if (options_.link.self.empty())
			options_.link.self = random_token();
		if (options_.link.room.empty())
			options_.link.room = random_token();
		if (options_.link.url.empty())
			options_.link.url = default_network_service;
		if (options_.profile.empty() ||
		    options_.profile.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") !=
		        std::string::npos)
			throw std::runtime_error("Invalid local server profile");
		member_ = options_.link.self;
		transport_ = open_peer_transport(options_.link);
	}
	void add_pack(const Bytes &archive) override
	{
		world_->add_pack(archive);
		refresh_source();
	}
	void replace_packs(const std::vector<Bytes> &archives) override
	{
		if (running() || recovery_pending())
			throw std::runtime_error("End the session before changing packs");
		auto candidate = make_host_world();
		for (const auto &archive : archives)
			candidate->add_pack(archive);
		generation_base_ += world_->generation() + 1;
		world_ = std::move(candidate);
		refresh_source();
	}
	void clear_packs() override
	{
		for (const auto &[id, client] : clients_)
			send(id, "refused", {{"reason", "Session ended; join again"}}, true);
		consensus_ = nullptr;
		journal_.reset();
		fleet_.reset();
		fleet_generation_ = 0;
		game_.reset();
		world_->clear_packs();
		source_.reset();
		console_source_.reset();
		clients_.clear();
		grants_ = Json::object();
		intents_.clear();
		local_sequences_.clear();
		epoch_ = random_token();
		proposal_.clear();
		fault_ = false;
		want_local_ = false;
		tick_due_ = false;
		last_proposal_was_tick_ = true;
	}
	const Catalog &catalog() const override
	{
		return world_->catalog();
	}
	void start(const GameSetup &setup) override
	{
		if (running() || recovery_pending())
			throw std::runtime_error("End the current session before starting another");
		world_->start_server(setup);
		refresh_source();
		begin_log();
	}
	bool running() const override
	{
		return world_->running() && (!fleet_ || fleet_->begun());
	}
	bool recovery_pending() const override
	{
		return fleet_ ? !fleet_->begun() : world_->recovery_pending();
	}
	Bytes save() const override
	{
		return fleet_ ? save_fleet_recovery(*world_, fleet_->prepared()->plan) : world_->save();
	}
	Bytes fleet_setup(const GameSetup &setup) const override
	{
		return export_fleet_setup(*world_, setup, options_.link.room);
	}
	void restore(const Bytes &archive) override
	{
		auto pack = read_pack(archive);
		if (pack.manifest().value("format", std::string{}) == "phoenix-fleet-setup")
		{
			const auto plan = parse_json(pack.files().at("fleet.json"), "fleet.json");
			install_fleet(read_fleet_setup(archive, plan.at("members").contains(options_.link.self) ? options_.link.self
			                                                                                        : std::string{}));
			return;
		}
		auto candidate = make_host_world();
		candidate->restore_container(pack.draft());
		clear_packs();
		generation_base_ += world_->generation();
		world_ = std::move(candidate);
		refresh_source();
	}
	void resume_local() override
	{
		if (fleet_)
			fleet_->start();
		else
		{
			world_->resume_server();
			begin_log();
		}
		want_local_ = true;
	}
	void authorise_lobby_member(const std::string &id, const LobbyAdmission &ticket) override
	{
		if (valid_identifier(id) && valid_hex_token(ticket.key) &&
		    (ticket.role == JoinRole::player || ticket.role == JoinRole::gm))
			options_.lobby_tickets[id] = ticket;
	}
	void activate_prepared() override
	{
		if (fleet_)
			fleet_->start();
		else
		{
			world_->resume_server();
			begin_log();
		}
	}
	void tick() override
	{
		// Remember one due step while a durable write is in flight. Repeated
		// input must not require the command queue to empty before time advances.
		tick_due_ = running() && available();
	}
	std::uint64_t ticks() const override
	{
		return world_->ticks();
	}
	std::uint64_t generation() const override
	{
		return generation_base_ + world_->generation();
	}
	bool command(const OperatingCommand &value) override
	{
		return console_command(1, value);
	}
	std::optional<Observation> observation() const override
	{
		return console_observation(1);
	}
	std::vector<BodyView> viewscreen() const override
	{
		return world_->viewscreen();
	}
	std::vector<SceneBody> scene() const override
	{
		return world_->scene();
	}
	Json scene_lighting() const override
	{
		return world_->scene_lighting();
	}
	Json public_board() const override
	{
		return world_->public_board();
	}
	Json scene_presentation() const override
	{
		return world_->scene_presentation();
	}
	std::optional<BodyView> focus_body() const override
	{
		if (!world_->running())
			return {};
		auto ship = world_->authoritative_world().ship_for_slot(world_->setup().local_slot);
		for (const auto &body : world_->viewscreen())
			if (ship == body.id)
				return body;
		return {};
	}
	Json inspection() const override
	{
		if (!running())
			return nullptr;
		try
		{
			return world_->authoritative_world().save();
		}
		catch (const std::exception &)
		{
			return nullptr;
		}
	}
	std::vector<ConsoleSeat> console_seats() const override
	{
		return game_ ? game_->seats(member_) : std::vector<ConsoleSeat>{};
	}
	bool claim_console(std::uint64_t route, const ConsoleAddress &address) override
	{
		return claim(local, route, address, false);
	}
	bool release_console(std::uint64_t route) override
	{
		return release(local, route, true);
	}
	bool console_command(std::uint64_t route, const OperatingCommand &command) override
	{
		auto seat = game_ ? game_->owned(member_, local, route) : nullptr;
		if (!seat)
			return false;
		auto &sequence = local_sequences_[route];
		sequence = std::max(sequence, seat->sequence);
		if (!game_->can_command(member_, local, route, seat->token, sequence + 1, command))
			return false;
		return intent({{"kind", "command"},
		               {"owner", local},
		               {"route", route},
		               {"lease", seat->token},
		               {"sequence", ++sequence},
		               {"target", command.target},
		               {"action", command.action},
		               {"value", command.value},
		               {"system", command.system},
		               {"controller", command.controller},
		               {"channel", command.channel},
		               {"generation", command.generation},
		               {"request_id", command.request_id},
		               {"arguments", command.arguments},
		               {"console", command.console}});
	}
	std::optional<Observation> console_observation(std::uint64_t route) const override
	{
		return running() && game_ ? game_->observe(member_, local, route) : std::nullopt;
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
		       ". " + (fleet_ ? fleet_->status() + ". " : "") + message_;
	}
	std::vector<std::pair<std::string, std::string>> lobby_controls() const override
	{
		std::vector<std::pair<std::string, std::string>> controls;
		if (fleet_ && !fleet_->begun())
			for (const auto &[member, slot] : fleet_->prepared()->plan.at("members").items())
				controls.emplace_back("network.member/" + member,
				                      "Use server " + member + " / " + slot.get<std::string>());
		if (running() && game_)
		{
			if (game_->owned(member_, local))
				controls.emplace_back("network.release", "Release local Console");
			else
				for (const auto &seat : game_->seats(member_))
					if (seat.available)
						controls.emplace_back("network.local/" + seat.address.console,
						                      "Claim local Console: " + seat.spec.station + " / " +
						                          seat.address.console);
			for (const auto &[id, client] : clients_)
				if (client.connected && !client.approved)
					controls.emplace_back((client.role == "gm" ? "network.gm/" : "network.approve/") + id,
					                      "Approve " + client.role + ": " + id);
		}
		return controls;
	}
	bool session_action(const std::string &action) override
	{
		if (action.starts_with("network.member/") && fleet_ && !fleet_->begun())
		{
			auto prepared = read_fleet_setup(fleet_->prepared()->bundle, action.substr(15));
			install_fleet(std::move(prepared));
			return true;
		}
		if (action == "network.local")
			return claim(local, 1, {}, true);
		if (action.starts_with("network.local/") && game_)
			for (const auto &seat : game_->seats(member_))
				if (seat.address.console == action.substr(14))
					return claim(local, 1, seat.address, true);
		if (action == "network.release")
			return release(local, 1, true);
		if (action.starts_with("network.gm/") || action.starts_with("network.approve/"))
		{
			auto found = clients_.find(action.substr(action.find('/') + 1));
			if (found != clients_.end() && !found->second.approved)
				return intent({{"kind", "client-grant"},
				               {"owner", found->first},
				               {"key", found->second.key},
				               {"nonce", found->second.nonce},
				               {"role", found->second.role}});
		}
		return false;
	}
	void poll() override
	{
		const auto current = std::chrono::steady_clock::now();
		const auto wall = std::max<std::int64_t>(
		    0, std::chrono::duration_cast<std::chrono::milliseconds>(current - last_poll_).count());
		last_poll_ += std::chrono::milliseconds(wall);
		now_ += wall;
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
			if (fleet_)
			{
				fleet_->poll();
				for (const auto &entry : fleet_->take_committed())
					apply(entry);
				if (fleet_->failed())
				{
					fault_ = true;
					message_ = fleet_->status();
				}
				if (fleet_generation_ != fleet_->admission_generation())
				{
					fleet_generation_ = fleet_->admission_generation();
					proposal_.clear();
					intents_.clear();
					local_sequences_.clear();
				}
			}
			else if (journal_)
			{
				journal_->poll();
				consensus_ = journal_->peer();
				if (consensus_)
				{
					consensus_->elapse(static_cast<unsigned>(std::min<std::int64_t>(1000, wall)));
					if (term_ != consensus_->term())
					{
						term_ = consensus_->term();
						proposal_.clear();
					}
					for (const auto &entry : consensus_->take_committed())
						apply(entry);
					journal_->flush();
				}
			}
			if (game_ && game_->checkpoint_ready() &&
			    (fleet_ ? fleet_->checkpoint_due() : consensus_ && consensus_->checkpoint_due()))
			{
				Json state{{"world", world_->authoritative_world().save()},
				           {"leases", game_->snapshot()},
				           {"grants", grants_}};
				const bool accepted = fleet_ ? fleet_->checkpoint(state) : consensus_->checkpoint(state);
				if (!accepted)
					throw std::runtime_error("Applied fleet checkpoint exceeds its supported state bound");
				if (!fleet_)
					journal_->flush();
			}
			if (!running() || !available())
				tick_due_ = false;
			if (available() && proposal_.empty())
			{
				const bool can_tick = tick_due_ && (fleet_ ? fleet_->leader() : !consensus_->pending_write());
				// Alternate under load so neither commands nor simulation starve.
				if (can_tick && (intents_.empty() || !last_proposal_was_tick_))
				{
					if (propose({{"kind", "tick"}, {"tick", ticks() + 1}}, true))
					{
						tick_due_ = false;
						last_proposal_was_tick_ = true;
					}
				}
				else if (!intents_.empty() && propose(intents_.front()))
				{
					intents_.pop_front();
					last_proposal_was_tick_ = false;
				}
			}
			if (want_local_ && available())
			{
				if (game_ && game_->owned(member_, local, 1))
					want_local_ = false;
				else if (proposal_.empty())
					claim(local, 1, {}, false);
			}
			for (auto &[id, client] : clients_)
			{
				if (now_ - client.seen > 3000)
					client.connected = false;
				if ((!client.connected || client.renew) && game_ && game_->owned(member_, id))
					release(id, 1, false);
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
