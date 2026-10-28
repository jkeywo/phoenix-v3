module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.information_widgets;
import phoenix.content.catalog;

import phoenix.ui.rml;
import phoenix.application.preferences;
namespace phoenix::message_ui
{
std::string asset(const std::string &relative)
{
#ifdef __EMSCRIPTEN__
	return "/assets/" + relative;
#else
	return std::string(GetApplicationDirectory()) + "assets/" + relative;
#endif
}
ConsoleMessageView conversation_message(const Json &message, const std::string &id, const Catalog &catalog)
{
	ConsoleMessageView row;
	row.id = id;
	row.sender = localise(message.at("name").get<std::string>());
	row.text = message.at("literal").get<bool>() ? message.at("text").get<std::string>()
	                                             : localise(message.at("text").get<std::string>());
	row.portrait = "messages/console-4.png";
	const auto portrait = message.value("portrait", std::string{});
	if (catalog.assets.contains(portrait))
		row.avatar = catalog.assets.at(portrait);
	return row;
}
std::string markup(const ConsoleMessageView &message, bool compact)
{
	const auto escape = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	const auto portrait = message.avatar.empty() ? asset(message.portrait) : register_menu_image(message.avatar);
	const std::string avatar = compact ? "48px" : "56px";
	return "<div style=\"display:block;position:relative;width:100%;box-sizing:border-box;min-height:" +
	       std::string(compact ? "52px;padding-left:64px;margin:4px 0;" : "64px;padding-left:72px;margin:12px 0;") +
	       "\">" + "<img src=\"" + escape(portrait) +
	       "\" style=\"display:block;position:absolute;left:0;top:0;width:" + avatar + ";height:" + avatar + ";\"/>" +
	       "<div style=\"display:block;width:100%;box-sizing:border-box;font-size:0.85em;margin-bottom:" +
	       std::string(compact ? "2px;" : "5px;") + "\">" + escape(message.sender) + "</div>" +
	       "<div "
	       "style=\"display:block;position:relative;width:100%;box-sizing:border-box;background:#fff7e5;color:#191919;"
	       "border:2px #191919;border-radius:12px;padding:" +
	       std::string(compact ? "6px;" : "10px;") + "\">" +
	       "<div "
	       "style=\"display:block;position:absolute;left:-7px;top:12px;width:10px;height:10px;background:#fff7e5;"
	       "border-left:2px #191919;border-bottom:2px #191919;transform:rotate(45deg);\"></div>" +
	       "<div class=\"message-text\" style=\"display:block;width:100%;box-sizing:border-box;font-family:Comic "
	       "Neue;font-size:1.1em;white-space:pre-wrap;word-break:break-word;\">" +
	       escape(message.text) + "</div></div></div>";
}
void refresh(Rml::ElementDocument &document, Rml::Element &panel, const std::vector<ConsoleMessageView> &messages,
             const Catalog *catalog, std::vector<std::pair<std::string, std::string>> &readouts,
             const std::string &prefix, bool compact)
{
	panel.SetProperty("display", "block");
	std::set<std::string> keep;
	for (const auto &message : messages)
	{
		const auto id = prefix + message.id;
		keep.insert(id);
		auto *row = document.GetElementById(id);
		if (!row)
		{
			auto created = document.CreateElement("div");
			created->SetId(id);
			created->SetProperty("display", "block");
			row = panel.AppendChild(std::move(created));
		}
		auto presented = message;
		if (catalog && catalog->assets.contains(message.portrait))
			presented.avatar = catalog->assets.at(message.portrait);
		const auto text = markup(presented, compact);
		if (row->GetAttribute<std::string>("data-message", "") != text)
		{
			row->SetInnerRML(text);
			row->SetAttribute("data-message", text);
		}
		readouts.emplace_back(id, message.accessible_text());
	}
	for (int i = panel.GetNumChildren() - 1; i >= 0; --i)
		if (!keep.contains(panel.GetChild(i)->GetId()))
			panel.RemoveChild(panel.GetChild(i));
}
} // namespace phoenix::message_ui
