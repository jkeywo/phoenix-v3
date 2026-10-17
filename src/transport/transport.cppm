module;
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

export module phoenix.transport;

export namespace phoenix
{
enum class LinkEvent
{
	signalling_ready,
	signalling_lost,
	discovered,
	discovery_lost,
	direct_ready,
	direct_lost,
	packet,
	error
};
struct LinkNotice
{
	LinkEvent kind;
	std::string peer, data;
	bool direct = false;
};
struct LinkOptions
{
	std::string url, room, self;
	bool force_relay = false;
	std::vector<std::string> ice_servers;
	std::string ca_certificate_file;
};
class PeerTransport
{
public:
	virtual ~PeerTransport() = default;
	virtual std::vector<LinkNotice> poll() = 0;
	// True means accepted by an available route, not delivered/admitted/committed.
	virtual bool send(const std::string &peer, const std::string &bytes) = 0;
};
std::unique_ptr<PeerTransport> open_peer_transport(const LinkOptions &options);
void verify_link_options(const LinkOptions &options);
} // namespace phoenix

// Shared adapter policy. These declarations belong to this module, but are not
// part of the application-facing peer transport interface.
namespace phoenix
{
inline constexpr std::size_t link_packet_limit = 65536;
inline constexpr std::size_t link_envelope_limit = 262144;
inline constexpr std::size_t link_buffer_limit = 4 * 1024 * 1024;
inline constexpr std::size_t link_queue_limit = 1024;
inline constexpr std::size_t link_queue_bytes = 8 * 1024 * 1024;
inline constexpr std::size_t link_peer_limit = 32;
inline constexpr std::size_t link_candidate_limit = 64;
enum class LinkRoute
{
	unavailable,
	direct,
	relay
};
struct LinkAction
{
	enum class Kind
	{
		initiate,
		signal
	} kind;
	std::string peer, data;
};
class LinkPolicy
{
	std::string self_;
	bool force_relay_;
	std::set<std::string> discovered_;
	void discover(const std::string &peer, std::vector<LinkNotice> &notices, std::vector<LinkAction> &actions);

public:
	explicit LinkPolicy(const LinkOptions &options);
	std::vector<LinkAction> receive(const std::string &bytes, std::vector<LinkNotice> &notices);
	LinkRoute route(const std::string &peer, const std::string &bytes, bool direct_open,
	                std::size_t direct_buffered) const;
};
std::string link_hello(const LinkOptions &options);
std::string link_envelope(const std::string &op, const std::string &peer, const std::string &data);
} // namespace phoenix
