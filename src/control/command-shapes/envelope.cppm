module;
#include <cstddef>
#include <optional>
export module phoenix.control.native_commands;
export import phoenix.contracts;
export namespace phoenix
{
// Envelope shape only: domain-specific identity, authority, generation, range
// and physical eligibility checks remain with the command's native workflow.
struct NativeCommandEnvelope
{
	bool allow_controller = true;
	bool allow_speaker = false;
	std::optional<std::size_t> text_limit;
};
bool native_command_envelope(const OperatingCommand &, NativeCommandEnvelope = {});
} // namespace phoenix
