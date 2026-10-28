module;
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module phoenix.tools;
export import phoenix.content.documents;
import phoenix.application;
export import phoenix.workshop.shell;
export import phoenix.workshop.test_client;

export namespace phoenix
{
bool probe_tool_presentation(const Application &app);
void begin_tools();
void draw_workshop(Application &app);
void draw_gm_region(Application &app);
void end_tools();
void open_workshop_pack(const std::string &path);
// Explicit development smoke; never called during ordinary authoring.
bool probe_workshop_controllers(unsigned stage);
bool probe_workshop_layout(unsigned stage);
bool probe_gameplay_controller_test(WorkshopDocument &, unsigned stage);
std::optional<GameplayTestRequest> take_gameplay_test_request();
void set_gameplay_authoring_vocabulary(const Json &);
void set_gameplay_test_feedback(const Json &);
void pump_browser_workshop_test(Application &);
} // namespace phoenix
