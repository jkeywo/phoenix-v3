module;
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.transport;
import phoenix.data;

namespace phoenix
{
LinkPolicy::LinkPolicy(const LinkOptions &options) : self_(options.self), force_relay_(options.force_relay)
{
}

void LinkPolicy::discover(const std::string &peer, std::vector<LinkNotice> &notices, std::vector<LinkAction> &actions)
{
	if (peer == self_ || !valid_identifier(peer) || discovered_.size() >= link_peer_limit)
		return;
	if (discovered_.insert(peer).second)
		notices.push_back({LinkEvent::discovered, peer});
	if (!force_relay_ && self_ < peer)
		actions.push_back({LinkAction::Kind::initiate, peer});
}

std::vector<LinkAction> LinkPolicy::receive(const std::string &bytes, std::vector<LinkNotice> &notices)
{
	std::vector<LinkAction> actions;
	if (bytes.size() > link_envelope_limit)
		throw std::runtime_error("Signalling envelope exceeds supported size");
	const auto value = nlohmann::json::parse(bytes);
	if (value.at("schema") != 1)
		return actions;
	const auto op = value.at("op").get<std::string>();
	if (op == "ready")
	{
		const auto &peers = value.at("peers");
		if (!peers.is_array() || peers.size() > link_peer_limit)
			throw std::runtime_error("Invalid discovery peer list");
		notices.push_back({LinkEvent::signalling_ready});
		for (const auto &peer : peers)
			discover(peer.get<std::string>(), notices, actions);
	}
	else if (op == "arrived")
		discover(value.at("peer").get<std::string>(), notices, actions);
	else if (op == "left")
	{
		const auto peer = value.at("peer").get<std::string>();
		if (valid_identifier(peer))
		{
			discovered_.erase(peer);
			notices.push_back({LinkEvent::discovery_lost, peer});
		}
	}
	else if (op == "signal" || op == "relay")
	{
		const auto peer = value.at("from").get<std::string>();
		const auto data = value.at("data").get<std::string>();
		if (!discovered_.contains(peer))
			return actions;
		if (data.size() > link_packet_limit || data.find('\0') != std::string::npos)
			throw std::runtime_error("Invalid routed envelope payload");
		if (op == "relay")
			notices.push_back({LinkEvent::packet, peer, data, false});
		else if (!force_relay_)
			actions.push_back({LinkAction::Kind::signal, peer, data});
	}
	else if (op == "error")
		notices.push_back({LinkEvent::error, {}, value.at("message").get<std::string>()});
	return actions;
}

LinkRoute LinkPolicy::route(const std::string &peer, const std::string &bytes, bool direct_open,
                            std::size_t direct_buffered) const
{
	if (bytes.size() > link_packet_limit || bytes.find('\0') != std::string::npos || !valid_identifier(peer) ||
	    peer == self_)
		return LinkRoute::unavailable;
	// Existing direct channels survive signalling loss and discovery departure.
	// Backpressure on an open direct channel must not reorder packets via relay.
	if (!force_relay_ && direct_open)
		return direct_buffered > link_buffer_limit ? LinkRoute::unavailable : LinkRoute::direct;
	return discovered_.contains(peer) ? LinkRoute::relay : LinkRoute::unavailable;
}

std::string link_hello(const LinkOptions &options)
{
	return nlohmann::json{{"schema", 1}, {"op", "hello"}, {"room", options.room}, {"peer", options.self}}.dump();
}

std::string link_envelope(const std::string &op, const std::string &peer, const std::string &data)
{
	if ((op != "signal" && op != "relay") || !valid_identifier(peer) || data.size() > link_packet_limit ||
	    data.find('\0') != std::string::npos)
		return {};
	auto bytes = nlohmann::json{{"schema", 1}, {"op", op}, {"to", peer}, {"data", data}}.dump();
	return bytes.size() <= link_envelope_limit ? bytes : std::string{};
}
} // namespace phoenix
