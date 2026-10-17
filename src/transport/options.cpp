#include <phoenix/transport.hpp>
#include <stdexcept>
namespace phoenix {
void verify_link_options(const LinkOptions& options) {
    const auto identity=[](const std::string& name) { return !name.empty() && name.size()<=128 && name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos; };
    const auto& url=options.url;
    if (!identity(options.self) || !identity(options.room) || url.size()>2048 || url.find_first_of("\r\n\t ") != std::string::npos || url.find('\0') != std::string::npos) throw std::runtime_error("Invalid transport identity or URL");
    bool secure=url.rfind("wss://",0) == 0, plain=url.rfind("ws://",0) == 0;
    if (!secure && !plain) throw std::runtime_error("Signalling requires WSS, or loopback WS for development");
    const std::size_t start=secure?6u:5u;
    const auto end=url.find_first_of("/?#",start);
    auto authority=url.substr(start,end == std::string::npos ? std::string::npos : end-start);
    if (authority.empty() || authority.find('@') != std::string::npos || url.find('#') != std::string::npos) throw std::runtime_error("Signalling URL must have a host, no userinfo and no fragment");
    if (plain) {
        auto split=authority.rfind(':'); if (split == std::string::npos) throw std::runtime_error("Development WS must name a loopback host and port");
        auto host=authority.substr(0,split), port=authority.substr(split+1);
        if ((host != "127.0.0.1" && host != "localhost" && host != "[::1]") || port.empty() || port.size()>5 || port.find_first_not_of("0123456789") != std::string::npos || std::stoul(port) == 0 || std::stoul(port)>65535) throw std::runtime_error("Plain WS is allowed only on loopback");
    }
    if (options.ice_servers.size()>8) throw std::runtime_error("At most eight ICE servers are supported");
    for (const auto& ice : options.ice_servers) if (ice.size()>1024 || ice.find('\0') != std::string::npos) throw std::runtime_error("Invalid ICE service URL");
}
}
