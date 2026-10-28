module;
#include <RmlUi/Core.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
export module phoenix.ui.pad_dispatch;

export import phoenix.ui.console_surface;
export import phoenix.ui.interaction;
export namespace phoenix
{
// Device-to-Console UI routing owns repeat, selection and edit state. Its explicit
// collaborators expose only current public surfaces, navigation DOM and input routes.
class PadDispatcher
{
public:
	PadDispatcher(InputRouter &, const std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> &, Rml::Context *&,
	              Rml::ElementDocument *&, const std::vector<ConsoleRegion> &, std::function<bool()> focused);
	~PadDispatcher();
	PadDispatcher(const PadDispatcher &) = delete;
	PadDispatcher &operator=(const PadDispatcher &) = delete;
	void cancel(const std::string &device);
	void sample(const Application &, const std::string &device, ConsoleId route, const std::string &control,
	            float value);
	void update(Application &);
	void draw(Application &);
	bool probe(Application &, unsigned stage);
	void reset();

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
