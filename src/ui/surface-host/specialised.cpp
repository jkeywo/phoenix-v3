module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
module phoenix.ui.surface_host;
namespace phoenix
{
namespace
{
std::string esc(const std::string &s)
{
	return Rml::StringUtilities::EncodeRml(s);
}
} // namespace
std::string SpecialisedPanel::id(const std::string &name) const
{
	return prefix + name;
}
std::string SpecialisedPanel::text(const std::string &value, const std::string &compact)
{
	const auto key = id("specialised/" + component + "/readout/" + std::to_string(readouts.size()));
	readouts.emplace_back(key, value);
	if (!compact.empty())
		compact_readouts[key] = compact;
	return "<p id=\"" + esc(key) + "\"></p>";
}
std::string SpecialisedPanel::button(const std::string &action, const std::string &label, std::optional<bool> selected,
                                     const std::string &visible, bool enabled)
{
	if (action.empty())
		return "<span>" + esc(label) + "</span>";
	const auto key = "task/specialised/" + component + "/" + std::to_string(buttons.size());
	if (enabled)
		actions[key] = action;
	else
		actions[key] = "";
	if (selected)
		toggles[id(key)] = *selected;
	buttons.emplace_back(id(key), label);
	return "<button id=\"" + esc(id(key)) + "\" class=\"" +
	       (action.starts_with("control-details/") ? "details-button " : "") +
	       (selected.value_or(false) ? "selected" : "") + "\"" + (enabled ? ">" : " disabled aria-disabled=\"true\">") +
	       (action.starts_with("control-details/") ? "<span class=\"details-icon\">i</span>"
	                                               : esc(visible.empty() ? label : visible)) +
	       "</button>";
}
} // namespace phoenix
