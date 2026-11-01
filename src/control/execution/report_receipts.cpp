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

module phoenix.control.execution;

namespace phoenix
{
void remember_controller_report(ConsoleControllerRuntime &controller, const std::string &message,
                                const KnowledgeRecord &record, KnowledgeTick tick)
{
	controller.report_receipts[controller_report_key(message, record)] = {tick, record.value.provenance.observed_tick,
	                                                                      record.value.provenance.revision,
	                                                                      controller_report_fingerprint(record)};
	while (controller.report_receipts.size() > 256)
	{
		const auto oldest =
		    std::min_element(controller.report_receipts.begin(), controller.report_receipts.end(),
		                     [](const auto &a, const auto &b)
		                     {
			                     return std::pair{a.second.sent_tick, a.first} < std::pair{b.second.sent_tick, b.first};
		                     });
		controller.report_receipts.erase(oldest);
	}
}

} // namespace phoenix
