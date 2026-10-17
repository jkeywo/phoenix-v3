module;
#include <algorithm>
#include <set>
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
constexpr std::size_t chunk_size = 12 * 1024, max_archive = 32 * 1024 * 1024, max_total = 64 * 1024 * 1024;
const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::size_t size(const Json &value, std::size_t maximum)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid pack transfer size/offset");
	auto number = value.get<std::uint64_t>();
	if (number > maximum)
		throw std::runtime_error("Pack transfer exceeds supported bounds");
	return static_cast<std::size_t>(number);
}
std::string base64(const Bytes &bytes)
{
	std::string result;
	for (std::size_t i = 0; i < bytes.size(); i += 3)
	{
		unsigned word = static_cast<unsigned>(bytes[i]) << 16;
		if (i + 1 < bytes.size())
			word |= static_cast<unsigned>(bytes[i + 1]) << 8;
		if (i + 2 < bytes.size())
			word |= bytes[i + 2];
		result += alphabet[(word >> 18) & 63];
		result += alphabet[(word >> 12) & 63];
		result += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
		result += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
	}
	return result;
}
Bytes unbase64(const std::string &text)
{
	if (text.empty() || text.size() % 4 || text.size() > 4 * ((chunk_size + 2) / 3))
		throw std::runtime_error("Invalid bounded pack chunk encoding");
	Bytes bytes;
	for (std::size_t i = 0; i < text.size(); i += 4)
	{
		unsigned word = 0;
		for (std::size_t j = 0; j < 4; ++j)
		{
			auto found = alphabet.find(text[i + j]);
			if (found == std::string::npos && text[i + j] != '=')
				throw std::runtime_error("Invalid pack chunk base64");
			word = (word << 6) | static_cast<unsigned>(found == std::string::npos ? 0 : found);
		}
		bytes.push_back(static_cast<std::uint8_t>(word >> 16));
		if (text[i + 2] != '=')
			bytes.push_back(static_cast<std::uint8_t>(word >> 8));
		if (text[i + 3] != '=')
			bytes.push_back(static_cast<std::uint8_t>(word));
	}
	if (base64(bytes) != text)
		throw std::runtime_error("Noncanonical pack chunk encoding");
	return bytes;
}
} // namespace
PackSource::PackSource(std::vector<Bytes> archives)
{
	if (archives.empty() || archives.size() > 32)
		throw std::runtime_error("Select one to 32 packs for transfer");
	std::size_t total = 0;
	for (const auto &archive : archives)
	{
		if (archive.empty() || archive.size() > max_archive || archive.size() > max_total - total)
			throw std::runtime_error("Selected transfer content exceeds 64 MiB total or 32 MiB per pack");
		total += archive.size();
	}
	std::vector<Pack> packs;
	for (auto &archive : archives)
		packs.push_back(read_pack(std::move(archive)));
	*this = PackSource(std::move(packs));
}
PackSource::PackSource(std::vector<Pack> packs) : packs_(std::move(packs))
{
	if (packs_.empty() || packs_.size() > 32)
		throw std::runtime_error("Select one to 32 packs for transfer");
	offer_ = {{"schema", 1}, {"packs", Json::array()}};
	std::size_t total = 0;
	for (const auto &pack : packs_)
	{
		const auto &archive = pack.archive();
		total += archive.size();
		if (archive.empty() || archive.size() > max_archive || total > max_total)
			throw std::runtime_error("Selected transfer content exceeds 64 MiB total or 32 MiB per pack");
		offer_["packs"].push_back({{"id", pack.manifest().at("id")}, {"hash", pack.hash()}, {"size", archive.size()}});
	}
	resolve_packs(packs_);
}
Json PackSource::offer() const
{
	return offer_;
}
Json PackSource::chunk(const Json &request) const
{
	auto index = size(request.at("pack"), packs_.size() - 1),
	     offset = size(request.at("offset"), packs_.at(index).archive().size());
	const auto &bytes = packs_.at(index).archive();
	if (offset >= bytes.size() || offset % chunk_size)
		throw std::runtime_error("Pack chunk offset must be aligned and inside its archive");
	auto end = std::min(bytes.size(), offset + chunk_size);
	return {{"pack", index},
	        {"hash", offer_.at("packs").at(index).at("hash")},
	        {"offset", offset},
	        {"data", base64(Bytes(bytes.begin() + offset, bytes.begin() + end))}};
}
void PackTransfer::validate_offer(const Json &offer)
{
	if (offer.at("schema") != 1 || !offer.at("packs").is_array() || offer.at("packs").empty() ||
	    offer.at("packs").size() > 32)
		throw std::runtime_error("Invalid pack offer");
	std::set<std::string> ids;
	std::size_t total = 0;
	for (const auto &entry : offer.at("packs"))
	{
		const std::string id = entry.at("id"), hash = entry.at("hash");
		auto length = size(entry.at("size"), max_archive);
		if (!length || !ids.insert(id).second || !valid_hex_token(hash) || length > max_total - total)
			throw std::runtime_error("Invalid or oversized ordered pack offer");
		total += length;
	}
}
PackTransfer::PackTransfer(const Json &offer, std::map<std::string, Bytes> cache)
{
	validate_offer(offer);
	for (const auto &entry : offer.at("packs"))
	{
		Pending pack;
		pack.id = entry.at("id");
		pack.hash = entry.at("hash");
		pack.length = size(entry.at("size"), max_archive);
		total_ += pack.length;
		auto found = cache.find(pack.hash);
		if (found != cache.end() && found->second.size() == pack.length && sha256(found->second) == pack.hash)
		{
			auto archive = read_pack(std::move(found->second));
			if (archive.manifest().at("id") != pack.id)
				throw std::runtime_error("Cached archive does not match offered pack identity");
			pack.verified = std::move(archive);
			pack.cached = true;
			received_ += pack.length;
		}
		else
		{
			pack.bytes.resize(pack.length);
			pack.received.resize((pack.length + chunk_size - 1) / chunk_size, false);
			pack.deadlines.resize(pack.received.size(), 0);
		}
		packs_.push_back(std::move(pack));
	}
	resolve();
}
std::vector<Json> PackTransfer::requests(std::uint64_t now, unsigned window)
{
	window = std::clamp(window, 1u, 8u);
	std::vector<Json> result;
	unsigned active = 0;
	for (const auto &pack : packs_)
		for (std::size_t chunk = 0; chunk < pack.received.size(); ++chunk)
			if (!pack.received[chunk] && pack.deadlines[chunk] > now)
				++active;
	for (std::size_t index = 0; index < packs_.size() && active < window; ++index)
	{
		auto &pack = packs_[index];
		for (std::size_t chunk = 0; chunk < pack.received.size() && active < window; ++chunk)
			if (!pack.received[chunk] && pack.deadlines[chunk] <= now)
			{
				pack.deadlines[chunk] = now + 2000;
				result.push_back({{"pack", index}, {"offset", chunk * chunk_size}});
				++active;
			}
	}
	return result;
}
bool PackTransfer::accept(const Json &chunk)
{
	auto index = size(chunk.at("pack"), packs_.size() - 1);
	auto &pack = packs_.at(index);
	if (chunk.at("hash") != pack.hash)
		throw std::runtime_error("Chunk belongs to different offered content");
	auto offset = size(chunk.at("offset"), pack.length);
	if (offset >= pack.length || offset % chunk_size)
		throw std::runtime_error("Invalid pack chunk offset");
	auto bytes = unbase64(chunk.at("data"));
	auto expected = std::min(chunk_size, pack.length - offset);
	if (bytes.size() != expected)
		throw std::runtime_error("Pack chunk length mismatch");
	auto block = offset / chunk_size;
	if (pack.verified || pack.received[block])
	{
		const auto &original = pack.verified ? pack.verified->archive() : pack.bytes;
		if (!std::equal(bytes.begin(), bytes.end(), original.begin() + offset))
			throw std::runtime_error("Conflicting repeated pack chunk");
		return false;
	}
	std::copy(bytes.begin(), bytes.end(), pack.bytes.begin() + offset);
	pack.received[block] = true;
	received_ += bytes.size();
	if (std::all_of(pack.received.begin(), pack.received.end(),
	                [](bool value)
	                {
		                return value;
	                }))
	{
		if (sha256(pack.bytes) != pack.hash)
			throw std::runtime_error("Transferred archive failed hash/identity verification");
		auto verified = read_pack(std::move(pack.bytes));
		if (verified.manifest().at("id") != pack.id)
			throw std::runtime_error("Transferred archive failed hash/identity verification");
		pack.verified = std::move(verified);
		resolve();
	}
	return true;
}
void PackTransfer::resolve()
{
	if (!std::all_of(packs_.begin(), packs_.end(),
	                 [](const Pending &pack)
	                 {
		                 return pack.verified.has_value();
	                 }))
		return;
	ordered_.clear();
	for (const auto &pack : packs_)
		ordered_.push_back(*pack.verified);
	catalog_ = resolve_packs(ordered_);
	ready_ = true;
}
bool PackTransfer::ready() const
{
	return ready_;
}
std::size_t PackTransfer::received_bytes() const
{
	return received_;
}
std::size_t PackTransfer::total_bytes() const
{
	return total_;
}
const Catalog &PackTransfer::catalog() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	return catalog_;
}
const std::vector<Pack> &PackTransfer::packs() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	return ordered_;
}
std::vector<Pack> PackTransfer::uncached_packs() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	std::vector<Pack> result;
	for (const auto &pack : packs_)
		if (!pack.cached)
			result.push_back(*pack.verified);
	return result;
}
} // namespace phoenix
