module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.information_widgets;
import phoenix.application.preferences;
namespace phoenix
{
std::optional<bool> MessageHistoryPanel::action(ConsoleDocument &dom, const std::string &action)
{
	if (action == "messages" || action == "messages-close" || action == "messages-arrival")
	{
		messages_open_ = action == "messages-arrival" || (action == "messages" && !messages_open_);
		if (action == "messages-arrival" && !message_queue_.empty())
		{
			message_filter_.clear();
			dom.document()
			    .GetElementById("messages-popup")
			    ->SetAttribute("data-message-focus", message_queue_.front().id);
			message_history_.read(message_queue_.front().id);
			message_queue_.erase(message_queue_.begin());
			message_until_ = 0;
		}
		if (!messages_open_)
			dom.document().GetElementById("messages-popup")->SetAttribute("data-message-return-focus", true);
		return true;
	}
	if (action.starts_with("messages-filter/"))
	{
		message_filter_ = action.substr(16);
		dom.document().GetElementById("message-history")->SetScrollTop(0);
		return true;
	}
	return std::nullopt;
}
MessageHistoryNotice MessageHistoryPanel::refresh(const std::vector<ConsoleMessageView> &messages,
                                                  MessageHistoryFrame frame)
{
	MessageHistoryNotice notice;
	auto &dom_ = frame.dom;
	auto *document_ = &frame.dom.document();
	auto *context_ = &frame.context;
	const auto id = [&](const std::string &name)
	{
		return frame.id(name);
	};
	const auto place = [&](const std::string &name)
	{
		frame.output.place(frame.dom, frame.context, frame.region, name);
	};
	const auto &scope = frame.scope;
	auto *control = document_->GetElementById("messages-control");
	auto *popup = document_->GetElementById("messages-popup");
	auto *arrival = document_->GetElementById("message-arrival");
	dom_.property(control, "display", frame.visible ? "block" : "none");
	if (!frame.visible)
	{
		dom_.property(popup, "display", "none");
		dom_.property(arrival, "display", "none");
		return notice;
	}
	// A generation change/reconnect establishes a baseline rather than replaying old arrivals.
	if (message_scope_ != scope)
	{
		message_scope_ = scope;
		message_queue_.clear();
		message_until_ = 0;
		messages_open_ = false;
	}
	auto arrivals = message_history_.update(messages, scope);
	bool urgent = false;
	std::string announcement;
	for (auto &row : arrivals)
	{
		urgent |= row.urgency >= 2;
		if (!announcement.empty())
			announcement += " ";
		announcement += row.accessible_text();
		message_queue_.push_back(std::move(row));
	}
	notice.announcement = std::move(announcement);
	notice.cue = urgent;
	std::erase_if(message_queue_,
	              [&](const auto &row)
	              {
		              return !message_history_.unread(row.id) ||
		                     std::none_of(message_history_.rows().begin(), message_history_.rows().end(),
		                                  [&](const auto &current)
		                                  {
			                                  return current.id == row.id;
		                                  });
	              });
	const auto unread = message_history_.unread_count();
	const auto label = localise("Messages") + (unread ? " (" + std::to_string(unread) + ")" : "");
	// These overlay controls publish their own semantics below. Do not add them
	// to the cached widget control list on every preparation.
	const auto message_button = [&](const std::string &action, const std::string &text)
	{
		return "<button id=\"" + Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
		       Rml::StringUtilities::EncodeRml(translate(text, frame.pseudo)) + "</button>";
	};
	if (!document_->GetElementById(id("messages")))
		control->SetInnerRML(message_button("messages", label));
	auto *toggle = document_->GetElementById(id("messages"));
	if (toggle->GetAttribute<std::string>("data-label", "") != label)
	{
		toggle->SetInnerRML(Rml::StringUtilities::EncodeRml(translate(label, frame.pseudo)));
		toggle->SetAttribute("data-label", label);
	}
	toggle->SetAttribute("aria-expanded", messages_open_ ? "true" : "false");
	frame.output.toggles[id("messages")] = messages_open_;
	popup->SetAttribute("aria-label", localise("Messages"));
	frame.output.controls.emplace_back(id("messages"), label);
	dom_.property(popup, "display", messages_open_ ? "block" : "none");
	if (messages_open_)
	{
		std::set<std::string> recipients;
		for (const auto &row : message_history_.rows())
			recipients.insert(row.recipient);
		if (!message_filter_.empty() && !recipients.contains(message_filter_))
			message_filter_.clear();
		auto markup = message_button("messages-close", localise("Close")) +
		              message_button("messages-filter/", localise("All Consoles"));
		for (const auto &recipient : recipients)
			markup += message_button("messages-filter/" + recipient, recipient);
		auto *header = document_->GetElementById("messages-header");
		if (header->GetAttribute<std::string>("data-markup", "") != markup)
		{
			header->SetInnerRML(markup);
			header->SetAttribute("data-markup", markup);
		}
		std::vector<ConsoleMessageView> rows;
		for (const auto &row : message_history_.rows())
			if (message_filter_.empty() || row.recipient == message_filter_)
				rows.push_back(row);
		auto *history = document_->GetElementById("message-history");
		message_ui::refresh(*document_, *history, rows, frame.catalog, frame.output.readouts, id("message-history/"),
		                    true);
		context_->Update();
		const auto requested = popup->GetAttribute<std::string>("data-message-focus", "");
		if (!requested.empty())
		{
			if (auto *element = document_->GetElementById(id("message-history/") + requested))
				element->ScrollIntoView();
			popup->RemoveAttribute("data-message-focus");
			context_->Update();
		}
		const auto top = history->GetAbsoluteOffset().y, bottom = top + history->GetClientHeight();
		for (const auto &row : rows)
			if (auto *element = document_->GetElementById(id("message-history/") + row.id))
			{
				const auto y = element->GetAbsoluteOffset().y;
				if (y < bottom && y + element->GetOffsetHeight() > top)
					message_history_.read(row.id);
				place(element->GetId());
			}
		frame.output.controls.emplace_back(id("messages-close"), localise("Close messages"));
		frame.output.controls.emplace_back(id("messages-filter/"), localise("All Consoles"));
		place(id("messages-close"));
		place(id("messages-filter/"));
		for (const auto &recipient : recipients)
		{
			frame.output.controls.emplace_back(id("messages-filter/" + recipient), recipient);
			place(id("messages-filter/" + recipient));
		}
	}
	const auto now = frame.now;
	if (!message_queue_.empty() && message_until_ && now >= message_until_)
	{
		message_queue_.erase(message_queue_.begin());
		message_until_ = 0;
	}
	const bool show = !messages_open_ && !message_queue_.empty();
	dom_.property(arrival, "display", show ? "block" : "none");
	if (show)
	{
		if (!message_until_)
			message_until_ = now + frame.seconds;
		const auto &row = message_queue_.front();
		const auto markup =
		    "<button id=\"" + id("messages-arrival") + "\">" + message_ui::markup(row, true) + "</button>";
		if (arrival->GetAttribute<std::string>("data-markup", "") != markup)
		{
			arrival->SetInnerRML(markup);
			arrival->SetAttribute("data-markup", markup);
		}
		frame.output.controls.emplace_back(id("messages-arrival"), row.accessible_text());
		context_->Update();
		place(id("messages-arrival"));
	}
	context_->Update();
	place(id("messages"));
	if (!messages_open_ && popup->GetAttribute<bool>("data-message-return-focus", false))
	{
		toggle->Focus();
		popup->RemoveAttribute("data-message-return-focus");
	}
	return notice;
}

void MessageHistoryPanel::reset_scope()
{
	message_history_ = MessageHistory{};
	message_queue_.clear();
	message_scope_.clear();
	messages_open_ = false;
	message_until_ = 0;
}
void MessageHistoryPanel::advance_clock(Rml::Context &context, double now)
{
	const auto over_arrival = [](Rml::Element *element)
	{
		for (; element; element = element->GetParentNode())
			if (element->GetId() == "message-arrival")
				return true;
		return false;
	};
	if (message_until_ &&
	    (messages_open_ || over_arrival(context.GetHoverElement()) || over_arrival(context.GetFocusElement())))
		message_until_ += std::max(0.0, now - message_last_time_);
	message_last_time_ = now;
}
} // namespace phoenix
