module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.conversation_panel;
import phoenix.ui.information_widgets;
import phoenix.application.preferences;
namespace phoenix
{
void ConversationPanel::invalidate_text()
{
	incoming_.clear();
}
void ConversationPanel::refresh(ConsoleModel &model, ConversationFrame frame)
{
	auto *document_ = &frame.dom.document();
	auto *context_ = &frame.context;
	const auto &region = frame.region;

	auto *root = document_->GetElementById("comms-layer");
	comms_widget_.clear();
	if (model.layout())
		for (const auto &w : model.layout()->at("widgets"))
			if (w.at("kind") == "conversations" && ui_widget_available(w, model.ui_availability()))
				comms_widget_ = w.at("id");
	const auto board = model.conversations();
	const bool active = (!comms_widget_.empty() || (model.layout() && model.layout()->contains("layout"))) &&
	                    !board.empty() && !frame.lobby;
	const bool authored = model.layout() && model.layout()->contains("layout");
	if (!authored)
		frame.dom.property(root, "display", active ? "block" : "none");
	if (!active)
		return;
	const auto board_signature = board.dump();
	if (board_signature != comms_board_signature_)
	{
		frame.dom.forget_elements();
		frame.dom.forget_properties();
		comms_board_signature_ = board_signature;
	}
	if (!authored)
	{
		frame.dom.property(document_->GetElementById("gameplay"), "display", "none");
		frame.dom.property(document_->GetElementById("widgets"), "display", "none");
	}
	const auto escape = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	const auto button = [&](const std::string &key, const std::string &label)
	{
		comms_controls_.emplace_back(key, label);
		return "<button id=\"" + escape(frame.id(key)) + "\">" + escape(label) + "</button>";
	};
	auto *compose = document_->GetElementById("comms-compose");
	if (!compose->HasChildNodes())
		compose->SetInnerRML("<input id=\"" + frame.id("comms/draft") +
		                     "\" type=\"text\" maxlength=\"4096\"/><button id=\"" + frame.id("comms/send") + "\">" +
		                     escape(localise("Send")) + "</button>");
	comms_controls_.clear();
	std::string listing;
	for (const auto &thread : board.at("threads"))
	{
		const auto key = thread.at("id").get<std::uint64_t>();
		auto title = localise(thread.at("title").get<std::string>());
		if (thread.value("unread", false))
			title = "* " + title;
		listing += button("comms/select/" + std::to_string(key), title);
	}
	listing += "<h3>" + escape(localise("Hail")) + "</h3>";
	for (const auto &p : board.at("directory"))
		if (p.value("available", false) && p.at("id") != "ship:" + std::to_string(model.observation()->own.id))
			listing += button("comms/hail/" + p.at("id").get<std::string>(),
			                  localise(p.at("name").get<std::string>()) + " (#" +
			                      std::to_string(p.at("host").get<EntityId>()) + ")");
	if (listing != comms_list_signature_)
	{
		frame.dom.forget_elements();
		frame.dom.forget_properties();
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("comms-list")->SetInnerRML(listing);
		comms_list_signature_ = listing;
		if (!focused.empty())
			if (auto *element = document_->GetElementById(focused))
				element->Focus();
	}
	const auto selected = board.value("selected", std::uint64_t{0});
	const Json *thread = nullptr;
	for (const auto &v : board.at("threads"))
		if (v.at("id") == selected)
			thread = &v;
	const bool split = authored && std::any_of(frame.layout.placements.begin(), frame.layout.placements.end(),
	                                           [](const auto &p)
	                                           {
		                                           return p.component.starts_with("conversation-");
	                                           });
	const auto panel_width =
	    authored ? std::max(100.f, root->GetClientWidth() / context_->GetDensityIndependentPixelRatio()) : region.width;
	const bool narrow = !split && panel_width < 700;
	const bool detail_visible = split
	                                ? frame.history_visible
	                                : (!authored || frame.whole_visible) && (!narrow || (thread && !comms_list_only_));
	if (!split)
	{
		frame.dom.property(document_->GetElementById("comms-list"), "width", narrow ? "100%" : "30%");
		frame.dom.property(document_->GetElementById("comms-list"), "display",
		                   narrow && thread && !comms_list_only_ ? "none" : "block");
		auto *conversation = document_->GetElementById("comms-conversation");
		frame.dom.property(conversation, "left", narrow ? "0%" : "32%");
		frame.dom.property(conversation, "width", narrow ? "100%" : "68%");
		frame.dom.property(conversation, "display", narrow && (!thread || comms_list_only_) ? "none" : "block");
	}
	const std::string width = "100%";
	std::string header = "<div style=\"display:block;width:" + width + ";height:56dp;\">" +
	                     button("comms/back", localise("Conversations")) +
	                     button("comms/screen", localise("On screen")) + "</div>";
	if (thread)
	{
		header += "<div style=\"display:block;width:" + width + ";font-size:1.1em;margin:4dp 0;\">" +
		          escape(localise(thread->at("title").get<std::string>())) + "</div>";
		header += "<div style=\"display:block;width:" + width + ";\">";
		bool first = true;
		for (const auto &p : thread->at("members"))
		{
			header += (first ? "" : " · ") + escape(localise(p.at("name").get<std::string>())) + " — " +
			          escape(localise(p.at("connected").get<bool>() ? "Connected" : "Out of contact"));
			first = false;
		}
		header += "</div>";
	}
	auto *head = document_->GetElementById("comms-header");
	if (!authored)
		frame.dom.property(head, "width", width);
	if (comms_header_signature_ != header)
	{
		frame.dom.forget_elements();
		frame.dom.forget_properties();
		head->SetInnerRML(header);
		comms_header_signature_ = header;
	}
	std::vector<ConsoleMessageView> messages;
	if (thread)
		for (const auto &m : thread->at("messages"))
		{
			messages.push_back(message_ui::conversation_message(
			    m, "comms/message/" + std::to_string(selected) + "/" + std::to_string(m.at("id").get<std::uint64_t>()),
			    *frame.catalog));
		}
	auto *history = document_->GetElementById("comms-history");
	const bool at_bottom = history->GetScrollTop() + history->GetClientHeight() >= history->GetScrollHeight() - 8;
	const auto readout_count = frame.output.readouts.size();
	message_ui::refresh(*document_, *history, messages, frame.catalog, frame.output.readouts, frame.id(""), true);

	if (thread)
		for (const auto &message : thread->at("messages"))
			if (auto *row = document_->GetElementById(frame.id("comms/message/" + std::to_string(selected) + "/" +
			                                                   std::to_string(message.at("id").get<std::uint64_t>()))))
			{
				frame.dom.property(row, "width", "100%");
				frame.dom.property(row, "box-sizing", "border-box");
				frame.dom.property(row, "margin-left", "0dp");
				frame.dom.property(row, "margin-right", "0dp");
			}

	if (!detail_visible)
		frame.output.readouts.resize(readout_count);
	if (detail_visible && thread && !thread->at("messages").empty())
	{
		const auto latest = thread->at("messages").back().at("id").get<std::uint64_t>();
		if (comms_read_thread_ != selected || comms_read_message_ != latest)
		{
			if (model.conversation_action("select", selected))
			{
				comms_read_thread_ = selected;
				comms_read_message_ = latest;
			}
		}
	}
	if (at_bottom || comms_visible_thread_ != selected)
		history->SetScrollTop(history->GetScrollHeight());
	comms_visible_thread_ = selected;
	std::string incoming;
	if (thread)
		for (const auto &p : thread->at("incoming"))
			incoming += localise(p.at("name").get<std::string>()) + ": " + localise("Incoming message") +
			            (frame.reduced_motion ? "..." : std::string(1 + static_cast<int>(GetTime() * 2) % 3, '.')) +
			            " ";
	if (incoming_ != incoming)
	{
		document_->GetElementById("comms-incoming")->SetInnerRML(escape(incoming));
		incoming_ = incoming;
	}
	std::string choices;
	if (thread)
	{
		std::set<std::uint64_t> prompts;
		for (const auto &c : thread->at("choices"))
			prompts.insert(c.value("prompt", thread->at("prompt")).get<std::uint64_t>());
		std::uint64_t previous = 0;
		for (const auto &c : thread->at("choices"))
		{
			const auto prompt = c.value("prompt", thread->at("prompt")).get<std::uint64_t>();
			if (prompts.size() > 1 && prompt != previous)
			{
				choices += "<h3>" + escape(localise(c.value("question", std::string{}))) + "</h3>";
				previous = prompt;
			}
			const auto key = prompts.size() > 1
			                     ? "comms/reply/" +
			                           std::to_string(c.value("conversation", thread->at("id")).get<std::uint64_t>()) +
			                           "/" + std::to_string(prompt) + "/" + c.at("id").get<std::string>()
			                     : "comms/answer/" + c.at("id").get<std::string>();
			if (c.value("enabled", true))
				choices += button(key, localise(c.at("text").get<std::string>()));
			else
				choices += "<p>" + escape(localise(c.at("text")) + " — " + localise(c.value("reason", "Unavailable"))) +
				           "</p>";
		}
	}
	if (choices != comms_choice_signature_)
	{
		frame.dom.forget_elements();
		frame.dom.forget_properties();
		document_->GetElementById("comms-choices")->SetInnerRML(choices);
		comms_choice_signature_ = choices;
	}
	const bool text =
	    (split ? frame.compose_visible : detail_visible) && thread && thread->value("text_allowed", false);
	if (!authored)
		frame.dom.property(document_->GetElementById("comms-compose"), "display", text ? "block" : "none");
	if (text)
	{
		comms_controls_.emplace_back("comms/send", localise("Send"));
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(frame.id("comms/draft"))))
		{
			frame.output.inputs.push_back({frame.id("comms/draft"), localise("Message"), field->GetValue(), false});
			frame.output.place(frame.dom, frame.context, region, frame.id("comms/draft"));
		}
	}
	for (const auto &[key, label] : comms_controls_)
	{
		const bool list_control = key.starts_with("comms/select/") || key.starts_with("comms/hail/");
		if (narrow && (list_control == detail_visible))
			continue;
		frame.output.controls.emplace_back(frame.id(key), label);
		frame.output.place(frame.dom, frame.context, region, frame.id(key));
	}
	frame.output.toggles[frame.id("comms/screen")] = model.is_on_screen(comms_widget_);
	if (auto *screen = document_->GetElementById(frame.id("comms/screen")))
	{
		screen->SetClass("on-screen-active", model.is_on_screen(comms_widget_));
		screen->SetAttribute("aria-pressed", model.is_on_screen(comms_widget_) ? "true" : "false");
	}

	if (detail_visible && !incoming.empty())
		frame.output.readouts.emplace_back("comms-incoming", localise("Incoming message"));
}
void ConversationPanel::fit(const ConsoleModel &model, ConversationFrame frame)
{
	auto *document_ = &frame.dom.document();
	auto *context_ = &frame.context;
	const auto &region = frame.region;

	if (!model.layout() || !model.layout()->contains("layout") || !frame.history_visible)
		return;
	auto *history = document_->GetElementById("comms-history");
	const float width = history->GetClientWidth() / context_->GetDensityIndependentPixelRatio();
	const auto units = [](float value)
	{
		return std::to_string(std::max(0.f, value)) + "dp";
	};
	for (const auto *name : {"comms-header", "comms-choices", "comms-incoming", "comms-compose"})
	{
		auto *panel = document_->GetElementById(name);
		frame.dom.property(panel, "width", units(width));
		frame.dom.property(panel, "box-sizing", "border-box");
	}
	auto *header = document_->GetElementById("comms-header");
	for (int i = 0; i < header->GetNumChildren(); ++i)
		frame.dom.property(header->GetChild(i), "width", units(width));
	Rml::ElementList buttons;
	header->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		frame.dom.property(button, "width", units(std::min(190.f, (width - 24) / 2)));
	buttons.clear();
	document_->GetElementById("comms-choices")->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		frame.dom.property(button, "width", units(width));
	if (auto *draft = document_->GetElementById(frame.id("comms/draft")))
		frame.dom.property(draft, "width", units(width - 140));
	context_->Update();
	frame.dom.forget_elements();
	for (const auto &[name, label] : comms_controls_)
		frame.output.place(frame.dom, frame.context, region, frame.id(name));
	frame.output.place(frame.dom, frame.context, region, frame.id("comms/draft"));
}
bool ConversationPanel::action(ConsoleModel &model, ConsoleDocument &dom, const std::string &prefix,
                               const std::string &action)
{
	auto *document_ = &dom.document();
	const auto panel_id = [&](const std::string &name)
	{
		return prefix + name;
	};

	if (!action.starts_with("comms/"))
		return false;
	const auto board = model.conversations();
	if (action == "comms/back")
	{
		comms_list_only_ = true;
		return true;
	}
	if (action == "comms/screen")
		return model.on_screen(comms_widget_, {0, 0, 1});
	if (action.starts_with("comms/hail/"))
	{
		comms_list_only_ = false;
		return model.conversation_action("hail", 0, action.substr(11));
	}
	if (action.starts_with("comms/select/"))
	{
		comms_list_only_ = false;
		return model.conversation_action("select", std::stoull(action.substr(13)));
	}
	if (action.starts_with("comms/reply/"))
	{
		const auto route = action.substr(12);
		const auto first = route.find('/'), second = first == std::string::npos ? first : route.find('/', first + 1);
		if (first == std::string::npos || second == std::string::npos)
			return false;
		return model.conversation_action("answer", std::stoull(route.substr(0, first)), route.substr(second + 1),
		                                 std::stoull(route.substr(first + 1, second - first - 1)));
	}
	if (action.starts_with("comms/answer/"))
		for (const auto &thread : board.value("threads", Json::array()))
			if (thread.at("id") == board.value("selected", std::uint64_t{0}))
				return model.conversation_action("answer", thread.at("id"), action.substr(13), thread.at("prompt"));
	if (action == "comms/send")
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(panel_id("comms/draft"))))
		{
			auto target = board.value("selected", std::uint64_t{0});
			for (const auto &thread : board.value("threads", Json::array()))
				if (thread.at("id") == target)
				{
					target = thread.value("text_conversation", Json(target)).get<std::uint64_t>();
					break;
				}
			if (model.conversation_action("text", target, field->GetValue()))
			{
				field->SetValue("");
				return true;
			}
		}
	return false;
}
} // namespace phoenix
