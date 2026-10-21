module;
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

export module phoenix.console_controls;
export import phoenix.contracts;

export namespace phoenix
{
struct EligibleConsoleCommand
{
	OperatingCommand command;
	Json descriptor;
};
// All inputs are permitted projections or a resolved, safe Console layout.
// Refusal is advisory; the authoritative host still validates at commit.
std::optional<EligibleConsoleCommand> eligible_console_command(const Observation &, const Json &layout,
                                                               const Json &public_board, OperatingCommand);
std::string manual_console_binding(const Observation &, const Json &layout, const std::string &action,
                                   const std::string &anchor_action);
} // namespace phoenix
