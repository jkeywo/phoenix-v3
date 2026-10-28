module;
#include <memory>
#include <optional>
#include <string>

export module phoenix.workshop.gameplay;
export import phoenix.content.documents;
export import phoenix.workshop.shell;
export import phoenix.workshop.test_client;

export namespace phoenix
{
class GameplayEditors
{
public:
	GameplayEditors();
	~GameplayEditors();
	GameplayEditors(const GameplayEditors &) = delete;
	GameplayEditors &operator=(const GameplayEditors &) = delete;
	void areas(WorkshopDocument &, bool pseudo, ToolMessage &, std::string &selection);
	void properties(WorkshopDocument &, bool pseudo, ToolMessage &, const std::string &selection,
	                const std::string &entity);
	void console_properties(WorkshopDocument &, bool pseudo, Json &);
	void set_vocabulary(const Json &);
	void set_feedback(const Json &);
	std::optional<GameplayTestRequest> take_request();
	bool probe_test(WorkshopDocument &, unsigned stage);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
