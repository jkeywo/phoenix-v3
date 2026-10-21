module;
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.inference;
import phoenix.wire;
namespace phoenix
{
struct InferenceConnection::Impl
{
	std::string token, epoch = random_token();
	std::unique_ptr<PeerTransport> link;
	Json providers = Json::array(), pending;
	std::vector<Json> results;
	std::string request_id;
	std::uint64_t latency_ms = 0;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(), seen{}, sent{}, hello{};
	bool paired = false;
	void send(const std::string &type, Json body)
	{
		link->send("bridge", encode_packet({"inference", "host", "bridge", type, std::move(body)}, token));
	}
	bool online() const
	{
		return paired && std::chrono::steady_clock::now() - seen < std::chrono::seconds(3);
	}
};
InferenceConnection::InferenceConnection(std::string token, std::string service) : impl_(std::make_unique<Impl>())
{
	if (!valid_hex_token(token))
		throw std::runtime_error("Invalid inference pairing token");
	impl_->token = token;
	if (service.empty())
		service = default_network_service;
	const auto room = "ai-" + sha256(Bytes(token.begin(), token.end())).substr(0, 32);
	impl_->link = open_peer_transport({service, room, "host"});
}
InferenceConnection::~InferenceConnection() = default;
Json InferenceConnection::status() const
{
	return {{"paired", impl_->paired},
	        {"online", impl_->online()},
	        {"providers", impl_->providers},
	        {"latency_ms", impl_->latency_ms},
	        {"configured", !impl_->providers.empty()},
	        {"pair", impl_->token}};
}
void InferenceConnection::reset_requests()
{
	impl_->pending = nullptr;
	impl_->results.clear();
	impl_->request_id.clear();
	impl_->epoch = random_token();
}
bool InferenceConnection::busy() const
{
	return !impl_->pending.is_null();
}
void InferenceConnection::poll()
{
	auto &s = *impl_;
	const auto now = std::chrono::steady_clock::now();
	for (const auto &notice : s.link->poll())
		if (notice.kind == LinkEvent::packet && notice.peer == "bridge")
			if (const auto packet = decode_packet(notice.data, "bridge", "host", "inference", s.token))
				try
				{
					const auto &body = packet->body;
					if (body.value("epoch", std::string{}) != s.epoch)
						continue;
					if (packet->type == "ai-capabilities" && (s.paired || now - s.started < std::chrono::seconds(120)))
					{
						const auto &providers = body.at("providers");
						bool valid = providers.is_array() && providers.size() <= 3;
						for (const auto &p : providers)
							valid = valid && (p == "anyjev" || p == "llamacpp" || p == "jev");
						if (valid)
						{
							s.providers = providers;
							s.paired = true;
							s.seen = now;
						}
					}
					else if (packet->type == "ai-result" && s.paired && !s.pending.is_null() &&
					         body.value("id", std::string{}) == s.request_id)
					{
						s.latency_ms =
						    checked_unsigned(body.value("latency_ms", Json(0)), 0, 3600000, "inference latency");
						bool offered = false;
						for (const auto &candidate : s.pending.at("candidates"))
							if (candidate.at("id") == body.value("candidate", std::string{}))
								offered = true;
						Json result{{"snapshot", s.pending},
						            {"candidate", body.value("candidate", std::string("defer_to_tree"))},
						            {"failed", body.contains("error") || !offered},
						            {"reason", "provider_failed"}};
						s.results.push_back(std::move(result));
						s.pending = nullptr;
						s.seen = now;
					}
				}
				catch (const std::exception &)
				{ /* malformed inference never faults a game */
				}
	if (now - s.hello > std::chrono::seconds(1))
	{
		s.send("ai-hello", {{"epoch", s.epoch}});
		s.hello = now;
	}
	if (!s.pending.is_null() && now - s.sent >= std::chrono::seconds(5))
	{
		s.results.push_back({{"snapshot", s.pending}, {"failed", true}, {"reason", "decision_timeout"}});
		s.pending = nullptr;
	}
}
bool InferenceConnection::request(const Json &snapshot)
{
	auto &s = *impl_;
	if (busy() || !s.online())
		return false;
	bool configured = false;
	for (const auto &provider : s.providers)
		if (provider == snapshot.at("provider"))
			configured = true;
	if (!configured || snapshot.dump().size() > 48000)
		return false;
	s.request_id = random_token();
	s.pending = snapshot;
	s.sent = std::chrono::steady_clock::now();
	s.send("ai-request",
	       {{"epoch", s.epoch}, {"id", s.request_id}, {"provider", snapshot.at("provider")}, {"snapshot", snapshot}});
	return true;
}
std::vector<Json> InferenceConnection::completed()
{
	auto result = std::move(impl_->results);
	impl_->results.clear();
	return result;
}
} // namespace phoenix
