module;
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

module phoenix.transport;
import phoenix.data;

namespace phoenix
{
std::string new_join_code()
{
	constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	std::string code;
	while (code.size() < 6)
	{
		const auto random = random_token();
		for (std::size_t at = 0; at + 1 < random.size() && code.size() < 6; at += 2)
		{
			const auto byte = std::stoul(random.substr(at, 2), nullptr, 16);
			if (byte < 252)
				code += alphabet[byte % 36];
		}
	}
	return code;
}
JoinTarget parse_join_code(const std::string &text)
{
	auto code = text;
	if (code.size() == 6 &&
	    code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == std::string::npos)
	{
		for (auto &c : code)
			if (c >= 'a' && c <= 'z')
				c -= 'a' - 'A';
		return {code, "host"};
	}
	const auto split = code.find('/');
	if (split == std::string::npos || !valid_identifier(code.substr(0, split)) ||
	    !valid_identifier(code.substr(split + 1)))
		throw std::runtime_error("Enter the six-character Join Code shown by the host");
	return {code.substr(0, split), code.substr(split + 1)};
}
std::string display_join_code(const std::string &room, const std::string &server)
{
	return room.size() == 6 && room.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == std::string::npos &&
	               server == "host"
	           ? room
	           : room + "/" + server;
}
void verify_link_options(const LinkOptions &options)
{
	const auto &url = options.url;
	if (!valid_identifier(options.self) || !valid_identifier(options.room) || url.size() > 2048 ||
	    url.find_first_of("\r\n\t ") != std::string::npos || url.find('\0') != std::string::npos)
		throw std::runtime_error("Invalid transport identity or URL");
	bool secure = url.rfind("wss://", 0) == 0, plain = url.rfind("ws://", 0) == 0;
	if (!secure && !plain)
		throw std::runtime_error("Signalling requires WSS, or loopback WS for development");
	const std::size_t start = secure ? 6u : 5u;
	const auto end = url.find_first_of("/?#", start);
	auto authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
	if (authority.empty() || authority.find('@') != std::string::npos || url.find('#') != std::string::npos)
		throw std::runtime_error("Signalling URL must have a host, no userinfo and no fragment");
	if (plain)
	{
		auto split = authority.rfind(':');
		if (split == std::string::npos)
			throw std::runtime_error("Development WS must name a loopback host and port");
		auto host = authority.substr(0, split), port = authority.substr(split + 1);
		if ((host != "127.0.0.1" && host != "localhost" && host != "[::1]") || port.empty() || port.size() > 5 ||
		    port.find_first_not_of("0123456789") != std::string::npos || std::stoul(port) == 0 ||
		    std::stoul(port) > 65535)
			throw std::runtime_error("Plain WS is allowed only on loopback");
	}
	if (options.ice_servers.size() > 8)
		throw std::runtime_error("At most eight ICE servers are supported");
	for (const auto &ice : options.ice_servers)
		if (ice.size() > 1024 || ice.find('\0') != std::string::npos)
			throw std::runtime_error("Invalid ICE service URL");
}
} // namespace phoenix
