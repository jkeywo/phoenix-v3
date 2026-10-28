module;
#include <functional>
#include <memory>
#include <optional>
#include <string>

export module phoenix.workshop.workflow;
export import phoenix.content.documents;
export import phoenix.menu;
export import phoenix.session.api;
export import phoenix.workshop.shell;
export import phoenix.workshop.test_client;

export namespace phoenix
{
struct WorkshopEnvironment
{
	std::shared_ptr<MenuLibrary> library;
	std::shared_ptr<SessionPort> session;
	std::string status;
	bool active = false, embedded = false;
	ToolFrame frame;
	ToolPreferences preferences;
	std::function<void(const std::string &)> navigate;
	std::function<void(const ToolPreferences &)> persist_preferences;
};
class WorkshopWorkspace
{
public:
	WorkshopWorkspace();
	~WorkshopWorkspace();
	WorkshopWorkspace(const WorkshopWorkspace &) = delete;
	WorkshopWorkspace &operator=(const WorkshopWorkspace &) = delete;
	void draw(WorkshopEnvironment &);
	void close();
	void open_pack(const std::string &);
	bool probe_layout(unsigned stage);
	bool probe_controllers(unsigned stage);
	bool probe_test(WorkshopDocument &, unsigned stage);
	void set_vocabulary(const Json &);
	void set_feedback(const Json &);
	std::optional<GameplayTestRequest> take_request();

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
