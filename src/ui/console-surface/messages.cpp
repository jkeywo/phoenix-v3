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
module phoenix.ui.console_surface;
import phoenix.content.ui;
import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.graphics.scene;
import phoenix.ui.theme;
import phoenix.ui.engineering_widgets;
import phoenix.ui.information_widgets;
import phoenix.ui.conversation_panel;
import phoenix.ui.surface_host;
import phoenix.ui.navigation_panel;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
#include "surface_state.hpp"
namespace phoenix
{
std::optional<bool> ConsoleSurface::State::message_action(const std::string &action)
{
	if (action == "messages" || action == "messages-close" || action == "messages-arrival")
	{
		messages_open_ = action == "messages-arrival" || (action == "messages" && !messages_open_);
		if (action == "messages-arrival" && !message_queue_.empty())
		{
			message_filter_.clear();
			document_->GetElementById("messages-popup")->SetAttribute("data-message-focus", message_queue_.front().id);
			message_history_.read(message_queue_.front().id);
			message_queue_.erase(message_queue_.begin());
			message_until_ = 0;
		}
		if (!messages_open_)
			document_->GetElementById("messages-popup")->SetAttribute("data-message-return-focus", true);
		return true;
	}
	if (action.starts_with("messages-filter/"))
	{
		message_filter_ = action.substr(16);
		document_->GetElementById("message-history")->SetScrollTop(0);
		return true;
	}
	return std::nullopt;
}
void ConsoleSurface::State::prepare_messages(Application &app, const ConsoleRegion &region, bool pregame)
{
	const auto observation = model_.observation();
	const bool visible = !pregame && !settings_open_ && observation;
	auto *control = document_->GetElementById("messages-control");
	auto *popup = document_->GetElementById("messages-popup");
	auto *arrival = document_->GetElementById("message-arrival");
	dom_.property(control, "display", visible ? "block" : "none");
	if (!visible)
	{
		dom_.property(popup, "display", "none");
		dom_.property(arrival, "display", "none");
		return;
	}
	// A generation change/reconnect establishes a baseline rather than replaying old arrivals.
	const auto scope = std::to_string(generation_) + "/" + std::to_string(observation->own.id);
	if (message_scope_ != scope)
	{
		message_scope_ = scope;
		message_queue_.clear();
		message_until_ = 0;
		messages_open_ = false;
	}
	auto arrivals = message_history_.update(model_.messages(), scope);
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
	if (!announcement.empty())
		app.accessible_status = announcement;
	if (urgent && app.message_cue && !app.message_muted)
		app.message_cue(route_);
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
		       Rml::StringUtilities::EncodeRml(translate(text, app.pseudo_locale)) + "</button>";
	};
	if (!document_->GetElementById(id("messages")))
		control->SetInnerRML(message_button("messages", label));
	auto *toggle = document_->GetElementById(id("messages"));
	if (toggle->GetAttribute<std::string>("data-label", "") != label)
	{
		toggle->SetInnerRML(Rml::StringUtilities::EncodeRml(translate(label, app.pseudo_locale)));
		toggle->SetAttribute("data-label", label);
	}
	toggle->SetAttribute("aria-expanded", messages_open_ ? "true" : "false");
	app.screen_toggles[id("messages")] = messages_open_;
	popup->SetAttribute("aria-label", localise("Messages"));
	app.screen_controls.emplace_back(id("messages"), label);
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
		message_ui::refresh(*document_, *history, rows, app.session ? &app.session->catalog() : nullptr,
		                    app.screen_readouts, id("message-history/"), true);
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
				place(app, region, element->GetId());
			}
		app.screen_controls.emplace_back(id("messages-close"), localise("Close messages"));
		app.screen_controls.emplace_back(id("messages-filter/"), localise("All Consoles"));
		place(app, region, id("messages-close"));
		place(app, region, id("messages-filter/"));
		for (const auto &recipient : recipients)
		{
			app.screen_controls.emplace_back(id("messages-filter/" + recipient), recipient);
			place(app, region, id("messages-filter/" + recipient));
		}
	}
	const auto now = GetTime();
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
			message_until_ = now + app.message_seconds;
		const auto &row = message_queue_.front();
		const auto markup =
		    "<button id=\"" + id("messages-arrival") + "\">" + message_ui::markup(row, true) + "</button>";
		if (arrival->GetAttribute<std::string>("data-markup", "") != markup)
		{
			arrival->SetInnerRML(markup);
			arrival->SetAttribute("data-markup", markup);
		}
		app.screen_controls.emplace_back(id("messages-arrival"), row.accessible_text());
		context_->Update();
		place(app, region, id("messages-arrival"));
	}
	context_->Update();
	place(app, region, id("messages"));
	if (!messages_open_ && popup->GetAttribute<bool>("data-message-return-focus", false))
	{
		toggle->Focus();
		popup->RemoveAttribute("data-message-return-focus");
	}
}
} // namespace phoenix
