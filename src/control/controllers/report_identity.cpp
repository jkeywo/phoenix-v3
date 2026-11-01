module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

module phoenix.control.controllers;
import phoenix.data;

namespace phoenix
{
std::string controller_report_key(const std::string &message, const KnowledgeRecord &record)
{
	return "report/" + message + "/" + std::to_string(record.value.schema) + "/" +
	       std::to_string(record.value.subject) + "/" + std::to_string(record.value.provenance.source);
}
std::string controller_report_fingerprint(const KnowledgeRecord &record)
{
	const auto &p = record.value.provenance;
	Json claim =
	    Json::array({record.value.schema, record.value.producer, record.value.subject, p.source, p.observed_tick,
	                 p.revision, p.uncertainty, static_cast<int>(p.kind), record.value.withdrawn});
	for (const auto &[key, value] : record.value.fields)
		std::visit(
		    [&](const auto &field)
		    {
			    claim.push_back(Json::array({key, field}));
		    },
		    value);
	const auto encoded = claim.dump();
	return sha256({reinterpret_cast<const std::uint8_t *>(encoded.data()), encoded.size()});
}

} // namespace phoenix
