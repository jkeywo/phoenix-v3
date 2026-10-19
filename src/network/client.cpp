module;
#include <chrono>
#include <algorithm>
#include <deque>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.network.client;
import phoenix.blobs;
import phoenix.session.api;
import phoenix.wire;
#include "projection.hpp"

namespace phoenix
{
namespace
{
class NetworkClient final : public SessionPort
{
	enum class JobType
	{
		bootstrap,
		profile,
		cache_read,
		cache_write
	};
	struct Job
	{
		JobType type;
		std::string key, offer;
		std::shared_ptr<const Bytes> bytes;
		std::uint64_t version = 0, request = 0;
	};
	NetworkOptions options_;
	std::unique_ptr<PeerTransport> transport_;
	std::unique_ptr<BlobStore> blobs_;
	std::unique_ptr<PackTransfer> transfer_;
	std::unique_ptr<StateTransfer> state_transfer_;
	std::string projection_stream_;
	std::deque<Job> jobs_;
	std::optional<Job> active_job_;
	std::map<std::string, Bytes> cache_;
	std::string profile_key_, key_, epoch_, nonce_ = random_token(), offer_id_, message_ = "Opening client profile",
	                                        lease_, station_;
	Json offer_, inspection_;
	Catalog empty_;
	std::optional<Observation> observation_;
	std::vector<ConsoleSeat> seats_;
	ConsoleAddress selected_seat_;
	bool blobs_ready_ = false, profile_durable_ = false, welcomed_ = false, ready_sent_ = false, started_ = false,
	     approved_ = false, quorum_ = false, confirm_ = false, fatal_ = false, recovering_ = false;
	std::uint64_t now_ = 0, last_hello_ = 0, last_heartbeat_ = 0, last_state_ = 0, tick_ = 0, sequence_ = 0,
	              job_id_ = 0, profile_version_ = 0;
	unsigned cache_pending_ = 0;
	std::uint64_t generation_ = 0;
	std::chrono::steady_clock::time_point last_poll_ = std::chrono::steady_clock::now();
	std::string role() const
	{
		return options_.inspector ? "gm" : "client";
	}
	void persist_profile()
	{
		profile_durable_ = false;
		auto data = json_bytes(Json{{"schema", 1},
		                            {"room", options_.link.room},
		                            {"server", options_.server},
		                            {"self", options_.link.self},
		                            {"key", key_},
		                            {"epoch", epoch_},
		                            {"nonce", nonce_},
		                            {"role", role()}});
		jobs_.push_front(
		    {JobType::profile, profile_key_, {}, std::make_shared<const Bytes>(std::move(data)), ++profile_version_});
	}
	void connect()
	{
		if (options_.link.self.empty())
			throw std::runtime_error("Client profile has no identity");
		transport_ = open_peer_transport(options_.link);
		last_hello_ = 0;
		message_ = "Connecting to the selected ship server";
	}
	bool send(const std::string &type, const Json &body)
	{
		auto payload = body;
		if (type.starts_with("state-"))
			payload["stream"] = projection_stream_;
		return transport_ && welcomed_ &&
		       transport_->send(options_.server,
		                        encode_packet({epoch_, options_.link.self, options_.server, type, payload}, key_));
	}
	void hello()
	{
		if (!transport_)
			return;
		Json body{{"role", role()}, {"nonce", nonce_}};
		if (!options_.admission.empty())
			body["admission"] =
			    encode_packet({"lobby", options_.link.self, options_.server, "hello", body}, options_.admission);
		if (!key_.empty())
			body["resume"] = encode_packet({"lobby", options_.link.self, options_.server, "hello", body}, key_);
		transport_->send(options_.server,
		                 encode_packet({"lobby", options_.link.self, options_.server, "hello", body}, ""));
		last_hello_ = now_;
	}
	void finish_cache()
	{
		if (cache_pending_ || offer_.is_null())
			return;
		transfer_ = std::make_unique<PackTransfer>(offer_, std::move(cache_));
		cache_.clear();
		if (transfer_->ready())
			content_ready();
	}
	void content_ready()
	{
		if (!transfer_ || !transfer_->ready())
			return;
		for (const auto &pack : transfer_->uncached_packs())
			jobs_.push_back({JobType::cache_write, pack.hash(), offer_id_, pack.archive_owner()});
		message_ = options_.inspector ? "Verified content; waiting for host GM approval"
		                              : "Verified content; waiting for station selection";
	}
	void blob_result(BlobResult result)
	{
		if (result.opened)
		{
			if (!result.error.empty())
				throw std::runtime_error(result.error);
			blobs_ready_ = true;
			jobs_.push_back({JobType::bootstrap, profile_key_});
			return;
		}
		if (!active_job_ || result.request != active_job_->request)
			return;
		auto job = std::move(*active_job_);
		active_job_.reset();
		if (job.type == JobType::bootstrap)
		{
			if (!result.error.empty())
				throw std::runtime_error("Client profile could not be verified: " + result.error);
			if (result.bytes)
			{
				auto profile = parse_json(*result.bytes, "client profile");
				if (profile.at("schema") != 1 || profile.at("room") != options_.link.room ||
				    profile.at("server") != options_.server || profile.at("role") != role())
					throw std::runtime_error("Stored client profile belongs to another connection");
				const std::string self = profile.at("self");
				if (!options_.link.self.empty() && self != options_.link.self)
					throw std::runtime_error("Stored client profile identity differs from the requested identity");
				options_.link.self = self;
				key_ = profile.at("key");
				epoch_ = profile.at("epoch");
				if (!key_.empty() && !valid_hex_token(key_))
					throw std::runtime_error("Stored client credential is invalid");
				recovering_ = !key_.empty();
				if (key_.empty())
					nonce_ = profile.value("nonce", nonce_);
				persist_profile();
			}
			else
			{
				if (options_.link.self.empty())
					options_.link.self = random_token();
				persist_profile();
			}
		}
		else if (job.type == JobType::profile)
		{
			if (!result.error.empty())
				throw std::runtime_error("Client credential was not persisted: " + result.error);
			if (job.version == profile_version_)
			{
				profile_durable_ = true;
				if (!transport_)
					connect();
			}
		}
		else if (job.type == JobType::cache_read && job.offer == offer_id_)
		{
			if (result.bytes && result.error.empty())
				cache_[job.key] = std::move(*result.bytes);
			if (cache_pending_)
				--cache_pending_;
			finish_cache();
		}
	}
	void packet(const std::string &bytes)
	{
		if (auto lobby = decode_packet(bytes, options_.server, options_.link.self, "lobby", ""))
		{
			if (lobby->type == "welcome")
			{
				if (lobby->body.at("nonce") != nonce_ || lobby->body.at("role") != role())
					return;
				const std::string key = lobby->body.at("key"), epoch = lobby->body.at("epoch");
				const std::string stream = lobby->body.at("stream");
				if (!valid_hex_token(stream))
					throw std::runtime_error("Invalid projection stream identity");
				if (projection_stream_ != stream)
				{
					state_transfer_.reset();
					projection_stream_ = stream;
				}
				if (!valid_hex_token(key) || !valid_hex_token(epoch))
					throw std::runtime_error("Invalid welcome credential/epoch");
				if (key_ != key || epoch_ != epoch)
				{
					if (epoch_ != epoch)
						++generation_;
					key_ = key;
					epoch_ = epoch;
					state_transfer_.reset();
					observation_.reset();
					inspection_ = nullptr;
					tick_ = 0;
					ready_sent_ = false;
					persist_profile();
				}
				welcomed_ = true;
				message_ = "Ship server connected; verifying selected content";
				send("offer-request", Json::object());
			}
			else if (lobby->type == "refused")
			{
				message_ = lobby->body.value("reason", std::string("Connection refused"));
				if (message_ == "Session ended; join again")
				{
					key_.clear();
					epoch_.clear();
					nonce_ = random_token();
					welcomed_ = false;
					ready_sent_ = false;
					recovering_ = false;
					observation_.reset();
					inspection_ = nullptr;
					transfer_.reset();
					offer_ = nullptr;
					persist_profile();
				}
			}
			return;
		}
		if (!welcomed_)
			return;
		auto packet = decode_packet(bytes, options_.server, options_.link.self, epoch_, key_);
		if (!packet)
			return;
		if (packet->type.starts_with("state-") && packet->body.value("stream", std::string{}) != projection_stream_)
			return;
		if (packet->type == "state-offer")
		{
			if (!transfer_ || !transfer_->ready() || !profile_durable_)
				return;
			const auto sequence = network::counter(packet->body.at("sequence"));
			if (!state_transfer_ || sequence > state_transfer_->sequence())
				state_transfer_ = std::make_unique<StateTransfer>(packet->body);
			if (state_transfer_->ready() && sequence == state_transfer_->sequence())
				send("state-ack", {{"sequence", sequence}});
			return;
		}
		if (packet->type == "state-chunk")
		{
			if (!state_transfer_ || state_transfer_->ready())
				return;
			if (!state_transfer_->accept(packet->body) || !state_transfer_->ready())
				return;
			send("state-ack", {{"sequence", state_transfer_->sequence()}});
			packet->type = "state";
			packet->body = state_transfer_->state();
		}
		const auto &body = packet->body;
		if (packet->type == "pack-offer")
		{
			const std::string token = body.at("offer");
			if (!valid_hex_token(token))
				return;
			if (token == offer_id_)
				return;
			offer_id_ = token;
			offer_ = body.at("content");
			transfer_.reset();
			ready_sent_ = false;
			cache_.clear();
			cache_pending_ = 0;
			// Validate the bounded offer before requesting cached records or allocating from it.
			PackTransfer::validate_offer(offer_);
			for (const auto &pack : offer_.at("packs"))
			{
				jobs_.push_back({JobType::cache_read, pack.at("hash"), offer_id_});
				++cache_pending_;
			}
			message_ = "Checking verified content cache";
		}
		else if (packet->type == "pack-chunk")
		{
			if (!transfer_ || body.at("offer") != offer_id_ || transfer_->ready())
				return;
			const bool was_ready = transfer_->ready();
			transfer_->accept(body);
			if (!was_ready && transfer_->ready())
				content_ready();
		}
		else if (packet->type == "state")
		{
			if (!transfer_ || !transfer_->ready() || !profile_durable_)
				return;
			const auto tick = network::counter(body.at("tick"));
			if (tick < tick_)
				return;
			tick_ = tick;
			last_state_ = now_;
			started_ = body.at("started");
			station_ = body.at("station");
			approved_ = body.at("approved");
			quorum_ = body.at("quorum");
			seats_.clear();
			if (body.contains("seats"))
			{
				if (!body["seats"].is_array() || body["seats"].size() > 32)
					throw std::runtime_error("Invalid Console roster");
				for (const auto &seat : body["seats"])
					seats_.push_back({{network::counter(seat.at("ship")), seat.at("seat")},
					                  {seat.at("seat"), seat.at("station"), seat.at("definition"), seat.at("controls"),
					                   seat.at("feeds")},
					                  seat.at("available")});
			}
			if (options_.inspector)
			{
				if (approved_ && body.contains("inspection"))
					inspection_ = body.at("inspection");
				else
					inspection_ = nullptr;
			}
			else if (body.contains("observation"))
			{
				auto view = network::read_observation(body.at("observation"), transfer_->catalog());
				const std::string lease = body.at("lease");
				if (!valid_hex_token(lease))
					return;
				if (lease_ != lease)
				{
					++generation_;
					lease_ = lease;
					sequence_ = 0;
				}
				observation_ = std::move(view);
				confirm_ = false;
				recovering_ = false;
			}
			else
			{
				observation_.reset();
				if (recovering_ && started_ && body.at("owner").get<bool>() && station_ == "disconnected" && quorum_)
					send("claim", {{"replace", false}});
			}
		}
		else if (packet->type == "claim-result")
		{
			if (!body.at("ok").get<bool>())
			{
				message_ = body.at("reason");
				confirm_ = body.value("confirm", false);
			}
			else
			{
				const std::string lease = body.at("lease");
				if (lease_ != lease)
				{
					lease_ = lease;
					sequence_ = 0;
				}
				message_ = "Station claim committed; waiting for its permitted projection";
			}
		}
		else if (packet->type == "command-result" || packet->type == "refused")
			message_ = body.value("reason", std::string("Request refused"));
	}

public:
	explicit NetworkClient(NetworkOptions options) : options_(std::move(options))
	{
		if (options_.link.url.empty())
			options_.link.url = default_network_service;
		if (options_.server.empty() || options_.link.room.empty())
		{
			message_ = "Enter room/server in the launcher network-code field";
			return;
		}
		const auto identity = options_.link.room + "/" + options_.server + "/" + options_.profile + "/" + role();
		profile_key_ = "client." + sha256(Bytes(identity.begin(), identity.end()));
		blobs_ = open_blob_store(options_.data_location + ".client");
	}
	void add_pack(const Bytes &) override
	{
		throw std::runtime_error("Client content is selected and transferred by its ship server");
	}
	void clear_packs() override
	{
		throw std::runtime_error("Leave this client connection through the launcher");
	}
	const Catalog &catalog() const override
	{
		return transfer_ && transfer_->ready() ? transfer_->catalog() : empty_;
	}
	void start(const GameSetup &) override
	{
		throw std::runtime_error("A client cannot start an authoritative game");
	}
	bool running() const override
	{
		return !fatal_ && (options_.inspector ? !inspection_.is_null() : observation_.has_value());
	}
	bool recovery_pending() const override
	{
		return false;
	}
	Bytes save() const override
	{
		throw std::runtime_error("Runtime saves belong to a ship server");
	}
	void restore(const Bytes &) override
	{
		throw std::runtime_error("Runtime recovery belongs to a ship server");
	}
	void resume_local() override
	{
		throw std::runtime_error("Claim a station through the ship server");
	}
	void tick() override
	{
	}
	std::uint64_t ticks() const override
	{
		return tick_;
	}
	std::uint64_t generation() const override
	{
		return generation_;
	}
	bool command(const OperatingCommand &command) override
	{
		if (options_.inspector || !observation_ || !quorum_ || !profile_durable_ ||
		    command.target != observation_->own.id)
			return false;
		// Semantic/meta commands are authored by the server and carry no public
		// free-form command language. Admit only an exact current descriptor.
		bool projected = false;
		const auto controls = observation_->gameplay.find("controls");
		if (controls != observation_->gameplay.end() && controls->is_array())
			for (const auto &descriptor : *controls)
				if (descriptor.is_object() && descriptor.contains("id") && descriptor.at("id").is_string() &&
				    !descriptor.at("id").get_ref<const std::string &>().empty() &&
				    descriptor.at("id").get_ref<const std::string &>().size() <= 256 && descriptor.contains("action") &&
				    descriptor.at("action") == command.action &&
				    descriptor.value("system", Json("")) == command.system && descriptor.contains("value") &&
				    descriptor.at("value").is_number_integer() && descriptor.at("value") == command.value &&
				    (!descriptor.contains("enabled") || descriptor.at("enabled") == true))
					projected = true;
		const auto *spec = operating_action(command.action);
		if ((!spec && !projected) || (spec && !operating_value_valid(*spec, command.value)))
			return false;
		auto system = command.system;
		if (spec)
		{
			if (system.empty())
			{
				const auto selected = select_system(observation_->systems, spec->system);
				if (!selected)
					return false;
				system = *selected;
			}
			else if (!projected && std::none_of(observation_->systems.begin(), observation_->systems.end(),
			                                    [&](const SystemView &value)
			                                    {
				                                    return value.id == system && value.controlled;
			                                    }))
				return false;
		}
		// The authoritative World still validates capability, dynamic conditions,
		// ownership and the lease when this ordinary request is committed.
		return send("command", {{"target", command.target},
		                        {"action", command.action},
		                        {"system", system},
		                        {"value", command.value},
		                        {"lease", lease_},
		                        {"sequence", ++sequence_}});
	}
	std::optional<Observation> observation() const override
	{
		return options_.inspector ? std::nullopt : observation_;
	}
	std::vector<ConsoleSeat> console_seats() const override
	{
		return seats_;
	}
	bool claim_console(std::uint64_t route, const ConsoleAddress &address) override
	{
		if (route != 1 || options_.inspector)
			return false;
		selected_seat_ = address;
		return send("claim", {{"ship", address.ship}, {"seat", address.console}, {"replace", false}});
	}
	bool release_console(std::uint64_t route) override
	{
		return route == 1 && !options_.inspector && send("release", Json::object());
	}
	std::vector<BodyView> viewscreen() const override
	{
		return {};
	}
	Json inspection() const override
	{
		return options_.inspector ? inspection_ : Json(nullptr);
	}
	bool networked() const override
	{
		return true;
	}
	std::string connection_status() const override
	{
		if (fatal_)
			return "Client stopped: " + message_;
		if (transfer_ && !transfer_->ready())
			return "Receiving verified content " + std::to_string(transfer_->received_bytes()) + " / " +
			       std::to_string(transfer_->total_bytes()) + " bytes";
		if (running())
			return options_.inspector ? "Host-approved read-only GM inspector"
			       : quorum_          ? "Connected to own ship server; station control is active"
			                          : "Own ship server is waiting for its majority";
		if (options_.inspector && ready_sent_ && !approved_)
			return "Verified content; waiting for host approval of GM inspection";
		return message_;
	}
	std::vector<std::pair<std::string, std::string>> lobby_controls() const override
	{
		if (options_.inspector || !ready_sent_ || !started_ || !quorum_)
			return {};
		if (observation_)
			return {{"network.release", "Release station"}};
		if (confirm_)
			return {{"network.confirm", "Confirm replacing the disconnected operator"}};
		if (seats_.size() <= 1)
			return {{"network.claim", "Claim the ship console"}};
		std::vector<std::pair<std::string, std::string>> controls;
		for (const auto &seat : seats_)
			if (seat.available)
				controls.emplace_back("network.claim/" + seat.address.console,
				                      "Claim Console: " + seat.spec.station + " / " + seat.address.console);
		return controls;
	}
	bool session_action(const std::string &action) override
	{
		if (options_.inspector)
			return false;
		if (action == "network.claim" || action == "network.confirm")
			return send("claim", {{"replace", action == "network.confirm"},
			                      {"ship", selected_seat_.ship},
			                      {"seat", selected_seat_.console}});
		if (action.starts_with("network.claim/"))
			for (const auto &seat : seats_)
				if (seat.address.console == action.substr(14))
					return claim_console(1, seat.address);
		if (action == "network.release")
			return send("release", Json::object());
		return false;
	}
	void poll() override
	{
		if (!blobs_ || fatal_)
			return;
		auto current = std::chrono::steady_clock::now();
		const auto wall = std::max<std::int64_t>(
		    0, std::chrono::duration_cast<std::chrono::milliseconds>(current - last_poll_).count());
		now_ += static_cast<std::uint64_t>(wall);
		last_poll_ += std::chrono::milliseconds(wall);
		try
		{
			if (auto result = blobs_->poll())
				blob_result(std::move(*result));
			if (blobs_ready_ && !active_job_ && !jobs_.empty())
			{
				auto job = std::move(jobs_.front());
				jobs_.pop_front();
				job.request = ++job_id_;
				const bool writing = job.type == JobType::profile || job.type == JobType::cache_write;
				const bool accepted =
				    writing ? blobs_->write(job.request, job.key, job.bytes) : blobs_->read(job.request, job.key);
				if (!accepted)
					throw std::runtime_error("Unexpected persistent client-store backpressure");
				active_job_ = std::move(job);
			}
			if (transport_)
			{
				for (const auto &event : transport_->poll())
				{
					if (event.kind == LinkEvent::packet && event.peer == options_.server)
						packet(event.data);
					else if (event.kind == LinkEvent::signalling_ready ||
					         (event.kind == LinkEvent::discovered && event.peer == options_.server))
						hello();
					else if (event.kind == LinkEvent::error)
						message_ = event.data;
				}
				if (!welcomed_ && now_ - last_hello_ >= 1000)
					hello();
				if (welcomed_ && profile_durable_)
				{
					if (now_ - last_heartbeat_ >= 500)
					{
						send("heartbeat", Json::object());
						if (offer_.is_null())
							send("offer-request", Json::object());
						last_heartbeat_ = now_;
					}
					if (transfer_)
					{
						if (state_transfer_ && !state_transfer_->ready())
							for (const auto &request : state_transfer_->requests(now_))
								send("state-request", request);
						for (auto request : transfer_->requests(now_))
						{
							request["offer"] = offer_id_;
							send("pack-request", request);
						}
						if (transfer_->ready() && (!ready_sent_ || now_ - last_state_ > 1000))
							ready_sent_ =
							    send("ready", {{"offer", offer_id_}, {"packs", transfer_->catalog().ordered_packs}});
					}
				}
				if (running() && now_ - last_state_ > 3000)
				{
					observation_.reset();
					inspection_ = nullptr;
					message_ = "Waiting for own ship server; no client host migration";
				}
			}
		}
		catch (const std::exception &error)
		{
			fatal_ = true;
			message_ = error.what();
			observation_.reset();
			inspection_ = nullptr;
		}
	}
};
} // namespace
std::shared_ptr<SessionPort> make_network_client(NetworkOptions options)
{
	return std::make_shared<NetworkClient>(std::move(options));
}
} // namespace phoenix
