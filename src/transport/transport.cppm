module;
#include <memory>
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
