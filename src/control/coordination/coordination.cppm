module;
#include <array>
#include <nlohmann/json.hpp>
#include <span>
#include <optional>
#include <stdexcept>
#include <string>

export module phoenix.control.coordination;
export import phoenix.control.controllers;

export namespace phoenix
{
inline constexpr std::array<const char *, 6> officer_operations{"power",  "cooling",  "manoeuvre",
                                                                "repair", "manifest", "scan"};
inline constexpr std::array<const char *, 5> scan_capabilities{"basic_identity", "contact_arcs", "shield_sectors",
                                                               "compartment_condition", "findings"};
inline constexpr std::array<const char *, 6> officer_statuses{"requested", "accepted", "refused",
                                                              "completed", "failed",   "cancelled"};
template <std::size_t N> std::size_t officer_index(const std::array<const char *, N> &names, const std::string &name)
{
	for (std::size_t i = 0; i < N; ++i)
		if (name == names[i])
			return i;
	throw std::runtime_error("Unknown officer protocol value: " + name);
}
std::optional<KnowledgeTick> scan_completion_evidence(const Observation &, const OfficerRequest &);
bool officer_supports(const ConsoleControllerRuntime &, OfficerOperation);
bool officer_continuing_operation(OfficerOperation);
const OfficerRequest *pending_officer_request(const ConsoleControllerRuntime &, KnowledgeTick now);
Json save_controller_coordination(const ConsoleControllerRuntime &);
void restore_controller_coordination(ConsoleControllerRuntime &, const Json &,
                                     std::span<const KnowledgeId> known_peers);
} // namespace phoenix
