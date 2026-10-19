module;
#include <picosha2.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.wire;

namespace phoenix
{
namespace
{
Bytes key_bytes(const std::string &key)
{
	if (!valid_hex_token(key))
		throw std::runtime_error("Protocol key must be 32 bytes of lowercase hexadecimal");
	Bytes bytes;
	for (std::size_t i = 0; i < key.size(); i += 2)
		bytes.push_back(static_cast<std::uint8_t>(std::stoul(key.substr(i, 2), nullptr, 16)));
	return bytes;
}
std::string mac(const std::string &key, const std::string &text)
{
	auto bytes = key_bytes(key);
	bytes.resize(64, 0);
	Bytes inner(64), outer(64);
	for (std::size_t i = 0; i < 64; ++i)
	{
		inner[i] = bytes[i] ^ 0x36;
		outer[i] = bytes[i] ^ 0x5c;
	}
	inner.insert(inner.end(), text.begin(), text.end());
	std::array<std::uint8_t, 32> digest{};
	picosha2::hash256(inner.begin(), inner.end(), digest.begin(), digest.end());
	outer.insert(outer.end(), digest.begin(), digest.end());
	return picosha2::hash256_hex_string(outer);
}
bool equal(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	unsigned difference = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
		difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
	return difference == 0;
}
Json object(const GamePacket &packet)
{
	if (!valid_identifier(packet.session) || !valid_identifier(packet.from) || !valid_identifier(packet.to) ||
	    !valid_identifier(packet.type) || !packet.body.is_object())
		throw std::runtime_error("Invalid game packet envelope");
	return {{"schema", 5},     {"session", packet.session}, {"from", packet.from},
	        {"to", packet.to}, {"type", packet.type},       {"body", packet.body}};
}
} // namespace
std::string encode_packet(const GamePacket &packet, const std::string &key)
{
	auto data = object(packet);
	data["mac"] = key.empty() ? "" : mac(key, data.dump());
	auto bytes = data.dump();
	if (bytes.size() > 65536)
		throw std::runtime_error("Game packet exceeds 64 KiB");
	return bytes;
}
std::optional<GamePacket> decode_packet(const std::string &bytes, const std::string &sender,
                                        const std::string &recipient, const std::string &session,
                                        const std::string &key)
{
	try
	{
		if (bytes.size() > 65536 || bytes.find('\0') != std::string::npos)
			return {};
		const auto data = Json::parse(bytes,
		                              [](int depth, Json::parse_event_t, Json &)
		                              {
			                              if (depth > 32)
				                              throw std::runtime_error("Game packet nesting exceeds limit");
			                              return true;
		                              });
		if (!data.is_object() || data.size() != 7 || data.at("schema") != 5 || data.at("from") != sender ||
		    data.at("to") != recipient || data.at("session") != session)
			return {};
		GamePacket packet{data.at("session"), data.at("from"), data.at("to"), data.at("type"), data.at("body")};
		auto unsigned_data = object(packet);
		const std::string proof = data.at("mac");
		if (key.empty())
		{
			if (!proof.empty() || (packet.type != "hello" && packet.type != "welcome" && packet.type != "refused"))
				return {};
		}
		else if (!equal(proof, mac(key, unsigned_data.dump())))
			return {};
		return packet;
	}
	catch (const std::exception &)
	{
		return {};
	}
}
} // namespace phoenix
