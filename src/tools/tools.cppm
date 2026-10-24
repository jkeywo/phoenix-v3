module;
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module phoenix.tools;
export import phoenix.presentation;
export import phoenix.content;

export namespace phoenix
{
// Keep source templates and parameters so changing language refreshes existing feedback.
struct ToolMessage
{
	std::string key;
	std::map<std::string, std::string> parameters;
	mutable std::string rendered;
	ToolMessage() = default;
	ToolMessage(const char *text) : key(text)
	{
	}
	ToolMessage(std::string text, std::map<std::string, std::string> values = {})
	    : key(std::move(text)), parameters(std::move(values))
	{
	}
	const char *c_str() const
	{
		const auto translated = localise(key);
		rendered.clear();
		for (std::size_t i = 0; i < translated.size();)
		{
			const auto end = translated[i] == '{' ? translated.find('}', i + 1) : std::string::npos;
			if (end != std::string::npos)
				if (const auto found = parameters.find(translated.substr(i + 1, end - i - 1));
				    found != parameters.end())
				{
					rendered += found->second;
					i = end + 1;
					continue;
				}
			rendered += translated[i++];
		}
		return rendered.c_str();
	}
};
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
void draw_workshop_areas(WorkshopDocument &, Application &, ToolMessage &, std::string &);
void draw_gameplay_properties(WorkshopDocument &, Application &, ToolMessage &, const std::string &,
                              const std::string &);
void draw_console_gameplay_properties(WorkshopDocument &, Application &, Json &);
void set_gameplay_authoring_vocabulary(const Json &);
void set_gameplay_test_feedback(const Json &);
void pump_browser_workshop_test(Application &);
} // namespace phoenix
