module;
#include <memory>
#include <optional>
#include <string>

module phoenix.tools;
import phoenix.content.documents;
import phoenix.workshop.workflow;

namespace phoenix
{
WorkshopWorkspace &tool_workspace();
void open_workshop_pack(const std::string &path)
{
	tool_workspace().open_pack(path);
}
bool probe_workshop_layout(unsigned stage)
{
	return tool_workspace().probe_layout(stage);
}
bool probe_workshop_controllers(unsigned stage)
{
	return tool_workspace().probe_controllers(stage);
}
void set_gameplay_authoring_vocabulary(const Json &vocabulary)
{
	tool_workspace().set_vocabulary(vocabulary);
}
bool probe_gameplay_controller_test(WorkshopDocument &document, unsigned stage)
{
	return tool_workspace().probe_test(document, stage);
}
void set_gameplay_test_feedback(const Json &feedback)
{
	tool_workspace().set_feedback(feedback);
}
std::optional<GameplayTestRequest> take_gameplay_test_request()
{
	return tool_workspace().take_request();
}
} // namespace phoenix
