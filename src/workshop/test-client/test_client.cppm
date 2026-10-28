module;
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module phoenix.workshop.test_client;
export import phoenix.content.documents;
export import phoenix.workshop.shell;

export namespace phoenix
{
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
class WorkshopTestClient
{
public:
	WorkshopTestClient();
	~WorkshopTestClient();
	WorkshopTestClient(const WorkshopTestClient &) = delete;
	WorkshopTestClient &operator=(const WorkshopTestClient &) = delete;
	void draw(WorkshopDocument &, ToolMessage &, bool pseudo);
	void set_feedback(const Json &);
	const Json &feedback() const;
	const std::string &inspected_controller() const;
	std::optional<GameplayTestRequest> take_request();
	bool probing() const;
	bool probe(WorkshopDocument &, unsigned stage);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
