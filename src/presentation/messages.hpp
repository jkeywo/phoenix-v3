#pragma once
namespace phoenix::message_ui
{
inline std::string asset(const std::string &relative)
{
#ifdef __EMSCRIPTEN__
	return "/assets/" + relative;
#else
	return std::string(GetApplicationDirectory()) + "assets/" + relative;
#endif
}
inline std::string markup(const ConsoleMessageView &message)
{
	const auto escape = [](const std::string &text)
	{
		return Rml::StringUtilities::EncodeRml(text);
	};
	const auto portrait = message.avatar.empty() ? asset(message.portrait) : register_menu_image(message.avatar);
	return "<div style=\"display:block;position:relative;min-height:64px;padding-left:72px;margin:12px 0;\">"
	       "<img src=\"" +
	       escape(portrait) +
	       "\" style=\"display:block;position:absolute;left:0;top:0;width:56px;height:56px;\"/>"
	       "<div style=\"display:block;font-size:0.85em;margin-bottom:5px;\">" +
	       escape(message.sender) +
	       "</div>"
	       "<div style=\"display:block;position:relative;background:#fff7e5;color:#191919;border:2px #191919;"
	       "border-radius:12px;padding:10px;\">"
	       "<div style=\"display:block;position:absolute;left:-7px;top:12px;width:10px;height:10px;"
	       "background:#fff7e5;border-left:2px #191919;border-bottom:2px #191919;transform:rotate(45deg);\"></div>"
	       "<div style=\"display:block;font-family:Comic "
	       "Neue;font-size:1.1em;white-space:pre-wrap;word-break:break-word;\">" +
	       escape(message.text) + "</div></div></div>";
}
// Keep existing rows mounted. Arriving messages neither recreate focused siblings
// nor reset the containing history's scroll position.
inline void refresh(Rml::ElementDocument &document, Rml::Element &panel,
                    const std::vector<ConsoleMessageView> &messages, Application &app, const std::string &prefix = {})
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
		const auto text = markup(message);
		if (row->GetAttribute<std::string>("data-message", "") != text)
		{
			row->SetInnerRML(text);
			row->SetAttribute("data-message", text);
		}
		app.screen_readouts.emplace_back(id, message.accessible_text());
	}
	for (int i = panel.GetNumChildren() - 1; i >= 0; --i)
		if (!keep.contains(panel.GetChild(i)->GetId()))
			panel.RemoveChild(panel.GetChild(i));
}
} // namespace phoenix::message_ui
