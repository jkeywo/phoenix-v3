module;
#include <RmlUi/Core.h>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
export module phoenix.ui.surface_host;
export import phoenix.input;
export import phoenix.ui.interaction;
export namespace phoenix
{
// One document's rendering caches. The surface owns document/context lifetime;
// panel owners borrow this service synchronously and never retain DOM pointers.
class ConsoleDocument
{
public:
	void bind(Rml::ElementDocument &document);
	Rml::ElementDocument &document() const;
	Rml::Element *element(const std::string &name);
	void property(Rml::Element *target, const std::string &name, const std::string &value);
	void leaf_markup(Rml::Element *target, const std::string &text);
	void forget_elements();
	void forget_properties();
	void prune_markup();

private:
	struct MarkupValue
	{
		Rml::ObserverPtr<Rml::Element> element;
		std::string text;
	};
	Rml::ElementDocument *document_ = nullptr;
	std::map<std::string, Rml::Element *> elements_;
	std::map<std::pair<Rml::Element *, std::string>, std::string> properties_;
	std::map<Rml::Element *, MarkupValue> leaf_markup_;
};
// Borrowed publication destinations, not an Application or session capability.
struct ConsolePanelOutput
{
	std::vector<std::pair<std::string, std::string>> &controls, &readouts;
	std::vector<TextControl> &inputs;
	std::map<std::string, bool> &toggles;
	std::unordered_map<std::string, ControlPlacement> &placements;
	void place(ConsoleDocument &document, Rml::Context &context, const ConsoleRegion &region, const std::string &name);
};
} // namespace phoenix
