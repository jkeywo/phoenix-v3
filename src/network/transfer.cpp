#include <phoenix/wire.hpp>
#include <algorithm>
#include <set>
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
PackSource::PackSource(std::vector<Bytes> archives) : archives_(std::move(archives))
{
	if (archives_.empty() || archives_.size() > 32)
		throw std::runtime_error("Select one to 32 packs for transfer");
	std::vector<Pack> verified;
	offer_ = {{"schema", 1}, {"packs", Json::array()}};
	std::size_t total = 0;
	for (const auto &archive : archives_)
	{
		total += archive.size();
		if (archive.empty() || archive.size() > max_archive || total > max_total)
			throw std::runtime_error("Selected transfer content exceeds 64 MiB total or 32 MiB per pack");
		verified.push_back(read_pack(archive));
		const auto &pack = verified.back();
		offer_["packs"].push_back({{"id", pack.manifest.at("id")}, {"hash", pack.hash}, {"size", archive.size()}});
	}
	resolve_packs(verified);
}
Json PackSource::offer() const
{
	return offer_;
}
Json PackSource::chunk(const Json &request) const
{
	auto index = size(request.at("pack"), archives_.size() - 1),
	     offset = size(request.at("offset"), archives_.at(index).size());
	const auto &bytes = archives_.at(index);
	if (offset >= bytes.size() || offset % chunk_size)
		throw std::runtime_error("Pack chunk offset must be aligned and inside its archive");
	auto end = std::min(bytes.size(), offset + chunk_size);
	return {{"pack", index},
	        {"hash", offer_.at("packs").at(index).at("hash")},
	        {"offset", offset},
	        {"data", base64(Bytes(bytes.begin() + offset, bytes.begin() + end))}};
}
PackTransfer::PackTransfer(const Json &offer, const std::map<std::string, Bytes> &cache)
{
	if (offer.at("schema") != 1 || !offer.at("packs").is_array() || offer.at("packs").empty() ||
	    offer.at("packs").size() > 32)
		throw std::runtime_error("Invalid pack offer");
	std::set<std::string> ids;
	for (const auto &entry : offer.at("packs"))
	{
		Pending pack;
		pack.id = entry.at("id");
		pack.hash = entry.at("hash");
		auto length = size(entry.at("size"), max_archive);
		if (!length || !ids.insert(pack.id).second || pack.hash.size() != 64 ||
		    pack.hash.find_first_not_of("0123456789abcdef") != std::string::npos || length > max_total - total_)
			throw std::runtime_error("Invalid or oversized ordered pack offer");
		total_ += length;
		pack.bytes.resize(length);
		pack.received.resize((length + chunk_size - 1) / chunk_size, false);
		pack.deadlines.resize(pack.received.size(), 0);
		auto found = cache.find(pack.hash);
		if (found != cache.end() && found->second.size() == length && sha256(found->second) == pack.hash)
		{
			auto archive = read_pack(found->second);
			if (archive.manifest.at("id") != pack.id)
				throw std::runtime_error("Cached archive does not match offered pack identity");
			pack.bytes = found->second;
			std::fill(pack.received.begin(), pack.received.end(), true);
			pack.complete = true;
			received_ += length;
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
	auto offset = size(chunk.at("offset"), pack.bytes.size());
	if (offset >= pack.bytes.size() || offset % chunk_size)
		throw std::runtime_error("Invalid pack chunk offset");
	auto bytes = unbase64(chunk.at("data"));
	auto expected = std::min(chunk_size, pack.bytes.size() - offset);
	if (bytes.size() != expected)
		throw std::runtime_error("Pack chunk length mismatch");
	auto block = offset / chunk_size;
	if (pack.received[block])
	{
		if (!std::equal(bytes.begin(), bytes.end(), pack.bytes.begin() + offset))
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
		if (sha256(pack.bytes) != pack.hash || read_pack(pack.bytes).manifest.at("id") != pack.id)
			throw std::runtime_error("Transferred archive failed hash/identity verification");
		pack.complete = true;
		resolve();
	}
	return true;
}
void PackTransfer::resolve()
{
	if (!std::all_of(packs_.begin(), packs_.end(),
	                 [](const Pending &pack)
	                 {
		                 return pack.complete;
	                 }))
		return;
	std::vector<Pack> ordered;
	for (const auto &pack : packs_)
		ordered.push_back(read_pack(pack.bytes));
	catalog_ = resolve_packs(ordered);
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
std::vector<Bytes> PackTransfer::archives() const
{
	if (!ready_)
		throw std::runtime_error("Content is not verified/ready");
	std::vector<Bytes> result;
	for (const auto &pack : packs_)
		result.push_back(pack.bytes);
	return result;
}
} // namespace phoenix
