module;
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module phoenix.tools;
export import phoenix.presentation;
export import phoenix.content;

export namespace phoenix
{
void begin_tools();
void draw_workshop(Application &app);
void draw_gm_region(Application &app);
void end_tools();
void open_workshop_pack(const std::string &path);
// Explicit development smoke; never called during ordinary authoring.
bool probe_workshop_controllers(unsigned stage);
bool probe_gameplay_controller_test(WorkshopDocument &, unsigned stage);
enum class GameplayTestAction
{
	Start,
	Pause,
	Step,
	Restart,
	Stop
};
struct GameplayTestRequest
{
	Bytes pack;
	std::vector<Bytes> dependencies;
	std::string mission, slot, ship;
	GameplayTestAction action = GameplayTestAction::Start;
};
std::optional<GameplayTestRequest> take_gameplay_test_request();
void draw_workshop_areas(WorkshopDocument &, Application &, std::string &, std::string &);
void draw_gameplay_properties(WorkshopDocument &, Application &, std::string &, const std::string &,
                              const std::string &);
void draw_console_gameplay_properties(WorkshopDocument &, Application &, Json &);
void set_gameplay_authoring_vocabulary(const Json &);
void set_gameplay_test_feedback(const Json &);
void pump_browser_workshop_test(Application &);
} // namespace phoenix
