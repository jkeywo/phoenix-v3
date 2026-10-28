module;
#include <memory>
#include <string>

export module phoenix.workshop.gm;
export import phoenix.session.api;
export import phoenix.workshop.shell;

export namespace phoenix
{
class GmInspector
{
public:
	GmInspector();
	~GmInspector();
	GmInspector(const GmInspector &) = delete;
	GmInspector &operator=(const GmInspector &) = delete;
	void clear();
	void draw(const std::shared_ptr<SessionPort> &, const std::string &status, const ToolFrame &);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
