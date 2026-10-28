module;
#include <cstddef>
#include <optional>
module phoenix.control.native_commands;
namespace phoenix
{
bool native_command_envelope(const OperatingCommand &command, NativeCommandEnvelope policy)
{
	return command.channel.empty() && !command.request_id && command.arguments.empty() &&
	       (policy.allow_controller || command.controller.empty()) &&
	       (policy.allow_speaker || (command.speaker_name.empty() && command.speaker_avatar.empty())) &&
	       (!policy.text_limit || command.text.size() <= *policy.text_limit);
}
} // namespace phoenix
