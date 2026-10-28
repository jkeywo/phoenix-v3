module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>
module phoenix.ui.surface_host;
namespace phoenix
{
void ConsoleDocument::bind(Rml::ElementDocument &document)
{
	document_ = &document;
	elements_.clear();
	properties_.clear();
	leaf_markup_.clear();
}
Rml::ElementDocument &ConsoleDocument::document() const
{
	return *document_;
}
Rml::Element *ConsoleDocument::element(const std::string &name)
{
	const auto found = elements_.find(name);
	if (found != elements_.end())
		return found->second;
	return elements_.emplace(name, document_->GetElementById(name)).first->second;
}
void ConsoleDocument::property(Rml::Element *target, const std::string &name, const std::string &value)
{
	auto &previous = properties_[{target, name}];
	if (previous != value)
	{
		target->SetProperty(name, value);
		previous = value;
	}
}
void ConsoleDocument::leaf_markup(Rml::Element *target, const std::string &text)
{
	auto &previous = leaf_markup_[target];
	if (previous.element.get() == target && previous.text == text)
		return;
	target->SetInnerRML(text);
	previous = {target->GetObserverPtr(), text};
}
void ConsoleDocument::forget_elements()
{
	elements_.clear();
}
void ConsoleDocument::forget_properties()
{
	properties_.clear();
}
void ConsoleDocument::prune_markup()
{
	std::erase_if(leaf_markup_,
	              [](const auto &entry)
	              {
		              return !entry.second.element;
	              });
}
void ConsolePanelOutput::place(ConsoleDocument &document, Rml::Context &context, const ConsoleRegion &region,
                               const std::string &name)
{
	if (auto *element = document.element(name))
	{
		const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
		placements[name] = {region.x + at.x,
		                    region.y + at.y,
		                    element->GetOffsetWidth(),
		                    element->GetOffsetHeight(),
		                    context.GetFocusElement() == element,
		                    !element->HasAttribute("disabled") &&
		                        !(dynamic_cast<Rml::ElementFormControl *>(element) &&
		                          dynamic_cast<Rml::ElementFormControl *>(element)->IsDisabled())};
	}
}
} // namespace phoenix
