module;
#include <stdexcept>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

module phoenix.persistence;
import phoenix.data;
#include "store_internal.hpp"

namespace phoenix::storage
{
std::string encode(const WireJson &transaction)
{
	auto json = transaction.dump();
	if (json.size() > 2 * 1024 * 1024)
		throw std::runtime_error("Peer journal record exceeds 2 MiB");
	auto hash = sha256(std::span(reinterpret_cast<const std::uint8_t *>(json.data()), json.size()));
	return hash + " " + json + "\n";
}
Recovered replay(const std::string &journal, bool atomic_records)
{
	if (journal.size() > max_journal)
		throw std::runtime_error("Peer journal exceeds 128 MiB; checkpoint before continuing");
	Recovered result;
	while (result.complete_bytes < journal.size())
	{
		auto end = journal.find('\n', result.complete_bytes);
		if (end == std::string::npos)
		{
			if (atomic_records || journal.size() - result.complete_bytes > 2 * 1024 * 1024 + 66)
				throw std::runtime_error("Incomplete atomic peer record");
			break;
		}
		auto line = std::string_view(journal).substr(result.complete_bytes, end - result.complete_bytes);
		if (line.size() < 66 || line.size() > 2 * 1024 * 1024 + 65 || line[64] != ' ')
			throw std::runtime_error("Malformed peer journal record");
		auto json = line.substr(65);
		auto hash = sha256(std::span(reinterpret_cast<const std::uint8_t *>(json.data()), json.size()));
		if (line.substr(0, 64) != hash)
			throw std::runtime_error("Peer journal integrity failure");
		auto transaction = WireJson::parse(json.begin(), json.end());
		apply_peer_write(result.state, transaction);
		result.complete_bytes = end + 1;
	}
	return result;
}
} // namespace phoenix::storage
