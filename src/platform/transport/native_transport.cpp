module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#endif
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <utility>

module phoenix.transport;

namespace phoenix
{
namespace
{
using Json = nlohmann::json;
enum class Event
{
	ws_open,
	ws_closed,
	ws_packet,
	signal,
	packet,
	channel,
	opened,
	lost,
	error
};
struct Incoming
{
	Event type;
	std::string peer, data;
	std::shared_ptr<rtc::DataChannel> channel;
};
struct Inbox
{
	std::mutex mutex;
	std::deque<Incoming> events;
	std::size_t bytes = 0;
	bool stopped = false, overflow = false;
	void push(Incoming event)
	{
		std::lock_guard lock(mutex);
		if (stopped)
			return;
		if (events.size() >= link_queue_limit || bytes + event.data.size() > link_queue_bytes)
		{
			overflow = true;
			return;
		}
		bytes += event.data.size();
		events.push_back(std::move(event));
	}
};
std::string system_roots()
{
#ifdef _WIN32
	HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
	if (!store)
		throw std::runtime_error("Cannot read Windows trusted roots for WSS");
	std::string pem;
	PCCERT_CONTEXT certificate = nullptr;
	while ((certificate = CertEnumCertificatesInStore(store, certificate)))
	{
		DWORD size = 0;
		if (!CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded, CRYPT_STRING_BASE64HEADER,
		                          nullptr, &size))
			continue;
		std::string encoded(size, '\0');
		if (CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded, CRYPT_STRING_BASE64HEADER,
		                         encoded.data(), &size))
		{
			while (!encoded.empty() && encoded.back() == '\0')
				encoded.pop_back();
			pem += encoded;
		}
		if (pem.size() > 8 * 1024 * 1024)
		{
			CertFreeCertificateContext(certificate);
			CertCloseStore(store, 0);
			throw std::runtime_error("Windows trust store exceeds supported WSS bundle size");
		}
	}
	CertCloseStore(store, 0);
	if (pem.empty())
		throw std::runtime_error("No Windows trusted roots available for WSS");
	return pem;
#else
	// libdatachannel accepts either a PEM string or a filename. Preserve the
	// distribution trust store, including locally installed enterprise roots.
	if (const auto *file = std::getenv("SSL_CERT_FILE"); file && *file)
	{
		if (!std::filesystem::is_regular_file(file))
			throw std::runtime_error("SSL_CERT_FILE does not name a CA bundle");
		return file;
	}
	for (const auto *file : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt"})
		if (std::filesystem::is_regular_file(file))
			return file;
	throw std::runtime_error("Cannot locate the system CA bundle for WSS; set SSL_CERT_FILE");
#endif
}
// Optional, process-local diagnostic. It records sizes rather than packet bodies,
// and never enters routing, admission or persisted gameplay state.
class NativeTrafficProfile
{
	using SampleClock = std::chrono::steady_clock;
	struct SnapshotCounters
	{
		std::string stream;
		std::uint64_t sequence = 0, count = 0, total_bytes = 0, maximum_bytes = 0;
	};
	struct Counters
	{
		std::uint64_t sent_messages = 0, sent_bytes = 0, received_messages = 0, received_bytes = 0;
		std::map<std::string, SnapshotCounters> snapshots;
	};
	std::filesystem::path start_, stop_, output_;
	std::string self_;
	std::array<std::array<Counters, 2>, 3> counters_{};
	std::array<std::uint64_t, 3> overflows_{}, rejected_{}, rejected_bytes_{};
	unsigned phase_ = 0;
	SampleClock::time_point begin_{}, end_{};
	bool incomplete_ = false;
	void update() noexcept
	{
		try
		{
			std::error_code error;
			if (!phase_ && std::filesystem::exists(start_, error))
			{
				phase_ = 1;
				begin_ = SampleClock::now();
			}
			if (phase_ == 1 && std::filesystem::exists(stop_, error))
			{
				phase_ = 2;
				end_ = SampleClock::now();
			}
			incomplete_ |= bool(error);
		}
		catch (...)
		{
			incomplete_ = true;
		}
	}

public:
	NativeTrafficProfile(const char *directory, const std::string &self)
	    : start_(std::filesystem::path(directory) / "measure.start"),
	      stop_(std::filesystem::path(directory) / "measure.stop"),
	      output_(std::filesystem::path(directory) / ("transport-" + self + ".json")), self_(self)
	{
	}
	void sent(const std::string &peer, const std::string &bytes, bool direct) noexcept
	{
		update();
		auto &counter = counters_[phase_][direct];
		++counter.sent_messages;
		counter.sent_bytes += bytes.size();
		// Offers are small metadata envelopes. Never parse projection chunks or
		// retain their bodies; stream/sequence are private deduplication keys.
		if (bytes.size() > 4096 || bytes.find("\"state-offer\"") == std::string::npos)
			return;
		try
		{
			const auto packet = Json::parse(bytes);
			if (packet.value("type", std::string{}) != "state-offer")
				return;
			const auto &offer = packet.at("body");
			const auto stream = offer.at("stream").get<std::string>();
			const auto sequence = offer.at("sequence").get<std::uint64_t>();
			const auto size = offer.at("size").get<std::uint64_t>();
			auto &sample = counter.snapshots[peer];
			if (sample.count && sample.stream == stream && sequence <= sample.sequence)
				return;
			sample.stream = stream;
			sample.sequence = sequence;
			++sample.count;
			sample.total_bytes += size;
			sample.maximum_bytes = std::max(sample.maximum_bytes, size);
		}
		catch (...)
		{
			incomplete_ = true;
		}
	}
	void rejected(std::size_t bytes) noexcept
	{
		update();
		++rejected_[phase_];
		rejected_bytes_[phase_] += bytes;
	}
	void received(const std::vector<LinkNotice> &notices, bool overflow) noexcept
	{
		update();
		overflows_[phase_] += overflow;
		for (const auto &notice : notices)
			if (notice.kind == LinkEvent::packet)
			{
				auto &counter = counters_[phase_][notice.direct];
				++counter.received_messages;
				counter.received_bytes += notice.data.size();
			}
	}
	~NativeTrafficProfile()
	{
		try
		{
			const auto finish = end_ == SampleClock::time_point{} ? SampleClock::now() : end_;
			Json result{{"self", self_},
			            {"incomplete", incomplete_},
			            {"capture_seconds", begin_ == SampleClock::time_point{}
			                                    ? 0.0
			                                    : std::chrono::duration<double>(finish - begin_).count()},
			            {"scope", "Encoded application messages accepted for send or returned by poll; direct and "
			                      "relay separated. Accepted send is not proven delivery. Excludes ICE, DTLS, SCTP, "
			                      "WebSocket, TLS, TCP and IP framing."}};
			constexpr std::array<const char *, 3> phases{"startup", "measured", "teardown"};
			for (unsigned phase = 0; phase < phases.size(); ++phase)
			{
				Json row{{"receive_overflow_polls", overflows_[phase]},
				         {"rejected_send_messages", rejected_[phase]},
				         {"rejected_send_bytes", rejected_bytes_[phase]}};
				for (unsigned route = 0; route < 2; ++route)
				{
					const auto &counter = counters_[phase][route];
					row[route ? "direct" : "relay"] = {{"accepted_send_messages", counter.sent_messages},
					                                   {"accepted_send_bytes", counter.sent_bytes},
					                                   {"received_messages", counter.received_messages},
					                                   {"received_bytes", counter.received_bytes}};
					for (const auto &[peer, snapshot] : counter.snapshots)
						row[route ? "direct" : "relay"]["offered_snapshots"][peer] = {
						    {"count", snapshot.count},
						    {"total_bytes", snapshot.total_bytes},
						    {"maximum_bytes", snapshot.maximum_bytes}};
				}
				result[phases[phase]] = std::move(row);
			}
			std::ofstream file(output_);
			file << result.dump(2) << '\n';
		}
		catch (...)
		{
			// Diagnostics must not alter transport shutdown.
		}
	}
};
class NativeTransport final : public PeerTransport
{
	struct Peer
	{
		std::shared_ptr<rtc::PeerConnection> connection;
		std::shared_ptr<rtc::DataChannel> channel;
		std::vector<rtc::Candidate> candidates;
		bool remote = false;
	};
	LinkOptions options_;
	LinkPolicy policy_;
	std::shared_ptr<Inbox> inbox_ = std::make_shared<Inbox>();
	std::shared_ptr<rtc::WebSocket> socket_;
	std::map<std::string, Peer> peers_;
	std::chrono::steady_clock::time_point retry_{};
	std::unique_ptr<NativeTrafficProfile> traffic_;
	void reset_peer(const std::string &id)
	{
		auto found = peers_.find(id);
		if (found == peers_.end())
			return;
		auto peer = std::move(found->second);
		peers_.erase(found);
		if (peer.channel)
		{
			peer.channel->resetCallbacks();
			peer.channel->close();
		}
		peer.connection->resetCallbacks();
		peer.connection->close();
	}
	void connect()
	{
		if (socket_)
		{
			socket_->resetCallbacks();
			socket_->forceClose();
		}
		rtc::WebSocketConfiguration config;
		config.maxMessageSize = link_envelope_limit;
		config.connectionTimeout = std::chrono::seconds(10);
		if (!options_.ca_certificate_file.empty())
			config.caCertificatePemFile = options_.ca_certificate_file;
		else if (options_.url.rfind("wss://", 0) == 0)
		{
			static const auto roots = system_roots();
			config.caCertificatePemFile = roots;
		}
		socket_ = std::make_shared<rtc::WebSocket>(config);
		const auto weak = std::weak_ptr<Inbox>(inbox_);
		socket_->onOpen(
		    [weak]
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::ws_open});
		    });
		socket_->onClosed(
		    [weak]
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::ws_closed});
		    });
		socket_->onError(
		    [weak](const std::string &error)
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::error, {}, error});
		    });
		socket_->onMessage(
		    [weak](rtc::message_variant message)
		    {
			    if (auto text = std::get_if<std::string>(&message))
				    if (auto inbox = weak.lock())
					    inbox->push({Event::ws_packet, {}, *text});
		    });
		retry_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		socket_->open(options_.url);
	}
	bool envelope(const std::string &op, const std::string &peer, const std::string &data)
	{
		if (!socket_ || !socket_->isOpen() || socket_->bufferedAmount() > link_buffer_limit)
			return false;
		const auto bytes = link_envelope(op, peer, data);
		if (bytes.empty())
			return false;
		socket_->send(bytes);
		return true;
	}
	void attach(const std::string &id, const std::shared_ptr<rtc::DataChannel> &channel)
	{
		if (channel->label() != "phoenix")
		{
			channel->close();
			return;
		}
		auto &peer = peers_.at(id);
		if (peer.channel && peer.channel != channel)
		{
			peer.channel->resetCallbacks();
			peer.channel->close();
		}
		peer.channel = channel;
		const auto weak = std::weak_ptr<Inbox>(inbox_);
		channel->onOpen(
		    [weak, id]
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::opened, id});
		    });
		channel->onClosed(
		    [weak, id]
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::lost, id});
		    });
		channel->onError(
		    [weak, id](const std::string &error)
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::error, id, error});
		    });
		channel->onMessage(
		    [weak, id](rtc::message_variant message)
		    {
			    if (auto text = std::get_if<std::string>(&message))
				    if (text->size() <= link_packet_limit)
					    if (auto inbox = weak.lock())
						    inbox->push({Event::packet, id, *text});
		    });
	}
	Peer &create(const std::string &id, bool offer)
	{
		auto found = peers_.find(id);
		if (found != peers_.end())
			return found->second;
		if (peers_.size() >= link_peer_limit)
			throw std::runtime_error("Direct peer capacity reached; use available relay routes");
		rtc::Configuration config;
		config.maxMessageSize = link_packet_limit;
		for (const auto &server : options_.ice_servers)
			config.iceServers.emplace_back(server);
		auto connection = std::make_shared<rtc::PeerConnection>(config);
		auto &peer = peers_[id];
		peer.connection = connection;
		const auto weak = std::weak_ptr<Inbox>(inbox_);
		connection->onLocalDescription(
		    [weak, id](const rtc::Description &description)
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::signal, id,
				                 Json{{"type", description.typeString()}, {"sdp", std::string(description)}}.dump()});
		    });
		connection->onLocalCandidate(
		    [weak, id](const rtc::Candidate &candidate)
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::signal, id,
				                 Json{{"candidate", std::string(candidate)}, {"mid", candidate.mid()}}.dump()});
		    });
		connection->onDataChannel(
		    [weak, id](std::shared_ptr<rtc::DataChannel> channel)
		    {
			    if (auto inbox = weak.lock())
				    inbox->push({Event::channel, id, {}, std::move(channel)});
		    });
		connection->onStateChange(
		    [weak, id](rtc::PeerConnection::State state)
		    {
			    if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
				    if (auto inbox = weak.lock())
					    inbox->push({Event::lost, id});
		    });
		if (offer)
			attach(id, connection->createDataChannel("phoenix"));
		return peer;
	}
	void signal(const std::string &id, const std::string &bytes)
	{
		const auto value = Json::parse(bytes);
		auto &peer = create(id, false);
		if (value.contains("sdp"))
		{
			const std::string type = value.at("type");
			if (type != "offer" && type != "answer")
				return;
			peer.connection->setRemoteDescription(rtc::Description(value.at("sdp").get<std::string>(), type));
			peer.remote = true;
			for (const auto &candidate : peer.candidates)
				peer.connection->addRemoteCandidate(candidate);
			peer.candidates.clear();
		}
		else if (value.contains("candidate"))
		{
			rtc::Candidate candidate(value.at("candidate").get<std::string>(), value.value("mid", std::string("0")));
			if (peer.remote)
				peer.connection->addRemoteCandidate(candidate);
			else if (peer.candidates.size() < link_candidate_limit)
				peer.candidates.push_back(std::move(candidate));
		}
	}

public:
	explicit NativeTransport(LinkOptions options) : options_(std::move(options)), policy_(options_)
	{
		verify_link_options(options_);
		if (const auto *directory = std::getenv("PHOENIX_TRANSPORT_PROFILE_DIRECTORY"); directory && *directory)
			try
			{
				traffic_ = std::make_unique<NativeTrafficProfile>(directory, options_.self);
			}
			catch (...)
			{
				// Optional profiling cannot prevent a normal connection.
			}
		connect();
	}
	~NativeTransport() override
	{
		{
			std::lock_guard lock(inbox_->mutex);
			inbox_->stopped = true;
		}
		if (socket_)
		{
			socket_->resetCallbacks();
			socket_->forceClose();
		}
		for (auto &[id, peer] : peers_)
		{
			if (peer.channel)
			{
				peer.channel->resetCallbacks();
				peer.channel->close();
			}
			peer.connection->resetCallbacks();
			peer.connection->close();
		}
	}
	std::vector<LinkNotice> poll() override
	{
		std::deque<Incoming> events;
		bool overflow;
		{
			std::lock_guard lock(inbox_->mutex);
			events.swap(inbox_->events);
			inbox_->bytes = 0;
			overflow = inbox_->overflow;
			inbox_->overflow = false;
		}
		std::vector<LinkNotice> notices;
		if (overflow)
			notices.push_back({LinkEvent::error, {}, "Transport receive backpressure; packets dropped"});
		for (const auto &event : events)
			try
			{
				switch (event.type)
				{
				case Event::ws_open:
					socket_->send(link_hello(options_));
					break;
				case Event::ws_closed:
					notices.push_back({LinkEvent::signalling_lost});
					retry_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					break;
				case Event::ws_packet:
				{
					for (const auto &action : policy_.receive(event.data, notices))
						if (action.kind == LinkAction::Kind::reset)
							reset_peer(action.peer);
						else if (action.kind == LinkAction::Kind::initiate)
							create(action.peer, true);
						else
							signal(action.peer, action.data);
					break;
				}
				case Event::signal:
					envelope("signal", event.peer, event.data);
					break;
				case Event::packet:
					notices.push_back({LinkEvent::packet, event.peer, event.data, true});
					break;
				case Event::channel:
					if (peers_.contains(event.peer))
						attach(event.peer, event.channel);
					break;
				case Event::opened:
					notices.push_back({LinkEvent::direct_ready, event.peer, {}, true});
					break;
				case Event::lost:
					notices.push_back({LinkEvent::direct_lost, event.peer});
					break;
				case Event::error:
					notices.push_back({LinkEvent::error, event.peer, event.data});
					break;
				}
			}
			catch (const std::exception &error)
			{
				notices.push_back({LinkEvent::error, event.peer, error.what()});
			}
		if (socket_->isClosed() && std::chrono::steady_clock::now() >= retry_)
			try
			{
				connect();
			}
			catch (const std::exception &error)
			{
				notices.push_back({LinkEvent::error, {}, error.what()});
			}
		if (traffic_)
			traffic_->received(notices, overflow);
		return notices;
	}
	bool send(const std::string &id, const std::string &bytes) override
	{
		auto peer = peers_.find(id);
		const auto channel = peer != peers_.end() ? peer->second.channel : nullptr;
		const auto route =
		    policy_.route(id, bytes, channel && channel->isOpen(), channel ? channel->bufferedAmount() : 0);
		if (route == LinkRoute::unavailable)
		{
			if (traffic_)
				traffic_->rejected(bytes.size());
			return false;
		}
		if (route == LinkRoute::direct)
		{
			try
			{
				channel->send(bytes);
				if (traffic_)
					traffic_->sent(id, bytes, true);
				return true;
			}
			catch (const std::exception &)
			{
			}
		}
		const bool accepted = policy_.route(id, bytes, false, 0) == LinkRoute::relay && envelope("relay", id, bytes);
		if (traffic_)
		{
			if (accepted)
				traffic_->sent(id, bytes, false);
			else
				traffic_->rejected(bytes.size());
		}
		return accepted;
	}
};
} // namespace
std::unique_ptr<PeerTransport> open_peer_transport(const LinkOptions &options)
{
	return std::make_unique<NativeTransport>(options);
}
} // namespace phoenix
