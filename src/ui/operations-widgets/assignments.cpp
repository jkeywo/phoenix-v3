module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.operations_widgets;

import phoenix.application.preferences;
import phoenix.ui.rml;
import phoenix.console_controls;

namespace phoenix
{
#include "state.hpp"
namespace
{
std::string escaped(const std::string &s)
{
	return Rml::StringUtilities::EncodeRml(s);
}
std::string portrait(const OperationsWidgetContext &context, const std::string &ref, const std::string &name)
{
	if (!context.catalog || !context.catalog->assets.contains(ref))
		return "<span class=\"assignment-portrait empty-portrait\" role=\"img\" aria-label=\"" + escaped(name) +
		       "\">?</span>";
	const auto source = register_menu_image(context.catalog->assets.at(ref));
	return "<img class=\"assignment-portrait\" src=\"" + escaped(source) + "\" alt=\"" + escaped(name) + "\"/>";
}
std::string readout(const OperationsWidgetContext &context, std::vector<std::pair<std::string, std::string>> &rows,
                    const std::string &text, const std::string &kind)
{
	const auto key = context.id(kind + "/readout/" + std::to_string(rows.size()));
	rows.emplace_back(key, text);
	return "<p id=\"" + escaped(key) + "\"></p>";
}
bool strict(const OperationsWidgetContext &context)
{
	return context.model.layout() && context.model.layout()->value("strict_widgets", false);
}
} // namespace
std::string OperationsWidgets::State::mission_markup(const OperationsWidgetContext &context)
{
	away_readouts_.clear();
	const auto missions = context.board("away_missions");
	auto line = [&](const std::string &s)
	{
		return readout(context, away_readouts_, s, "away");
	};
	std::string html =
	    "<div class=\"mission-workspace\"><div class=\"mission-list\"><h2>" + localise("Missions") + "</h2>";
	for (const auto &m : missions)
		html += context.button("mission/select/" + m.at("id").get<std::string>(),
		                       localise(m.at("title")) + " — " + localise(m.at("phase")));
	html += "</div><div class=\"mission-detail\">";
	const auto selected = std::find_if(missions.begin(), missions.end(),
	                                   [&](const auto &m)
	                                   {
		                                   return m.at("id") == mission_selected_;
	                                   });
	if (selected == missions.end())
		return html + line(localise("Select a mission")) + "</div></div>";
	const auto &m = *selected;
	const auto identity = m.at("id").get<std::string>();
	const auto phase = m.at("phase").get<std::string>();
	const auto labels = m.value("labels", Json::object());
	const auto portraits = m.value("portraits", Json::object());
	const auto names = m.value("crew_names", Json::object());
	html += "<h2>" + escaped(localise(m.at("title"))) + "</h2>" + line(localise(phase)) + line(localise(away_notice_));
	if (m.value("result", std::string{}) != "")
		html += line(localise(m.at("result")));
	if (phase == "available" && m.value("dispatch", false))
		html += context.button("away/open/" + identity + "/", localise("Activate"));
	std::set<std::pair<std::string, std::string>> shown_people;
	html += "<div class=\"mission-roster\">";
	for (const auto &slot : m.at("slots"))
	{
		const auto key = slot.at("id").get<std::string>();
		const auto asset = slot.at("assigned").get<std::string>();
		shown_people.emplace(names.value(asset, std::string{}), portraits.value(asset, std::string{}));
		html += "<div class=\"assignment-tile\">" + portrait(context, portraits.value(asset, std::string{}),
		                                                     names.value(asset, labels.value(asset, std::string{})));
		const auto label =
		    localise(key) + ": " +
		    names.value(asset, localise(labels.value(asset, asset.empty() ? std::string("Unassigned") : asset)));
		if (!slot.at("choices").empty() || slot.value("owned", false))
			html += context.button("mission/slot/" + identity + "/" + key, label);
		else
			html += line(label);
		html += "</div>";
	}
	for (const auto &officer : m.at("accompanying"))
	{
		const auto asset = "officer:" + officer.get<std::string>();
		if (!shown_people.emplace(names.value(asset, std::string{}), portraits.value(asset, std::string{})).second)
			continue;
		html +=
		    "<div class=\"assignment-tile\">" +
		    portrait(context, portraits.value(asset, std::string{}), names.value(asset, labels.value(asset, asset))) +
		    line(names.value(asset, labels.value(asset, asset))) + "</div>";
	}
	const auto transport = m.value("transport", std::string{});
	const auto delivery_ref = context.model.layout()->value("delivery_portrait", std::string{});
	html += "<div class=\"assignment-tile\">" + portrait(context, delivery_ref, localise("Delivery method"));
	const auto delivery_label =
	    localise("Delivery") + ": " +
	    localise(labels.value(transport, transport.empty() ? std::string("Preparing") : transport));
	if (!m.at("transports").empty())
		html += context.button("mission/transport/" + identity, delivery_label);
	else
		html += line(delivery_label);
	html += "</div></div>";
	if (!mission_picker_.empty())
	{
		html += "<div class=\"assignment-picker\"><h3>" + localise("Eligible assignments") + "</h3>";
		if (mission_picker_ == "transport")
			for (const auto &choice : m.at("transports"))
				html += context.button("away/transport/" + identity + "/" + choice.get<std::string>(),
				                       localise(labels.value(choice.get<std::string>(), choice.get<std::string>())));
		else
			for (const auto &slot : m.at("slots"))
				if (slot.at("id") == mission_picker_)
				{
					if (slot.value("owned", false))
						html += context.button("away/withdraw/" + identity + "/" + mission_picker_,
						                       localise("Withdraw assignment"));
					for (const auto &choice : slot.at("choices"))
					{
						const auto asset = choice.get<std::string>();
						html += portrait(context, portraits.value(asset, std::string{}),
						                 names.value(asset, labels.value(asset, asset))) +
						        context.button("away/assign/" + identity + "/" + mission_picker_ + "/" + asset,
						                       names.value(asset, localise(labels.value(asset, asset))));
					}
				}
		html += context.button("mission/close-picker", localise("Close")) + "</div>";
	}
	if (m.value("launch_ready", false))
		html += context.button("away/send/" + identity + "/", localise("Launch"));
	else if (phase == "open" || phase == "recovered")
		html += line(localise(m.value("launch_blocker", std::string("Preparing"))));
	if (phase == "deployed" && m.value("dispatch", false))
		html += context.button("away/recall/" + identity + "/", localise("Recall"));
	if ((phase == "open" || phase == "outbound" || phase == "inbound") && m.value("dispatch", false))
		html += context.button("away/cancel/" + identity + "/", localise("Cancel"));
	return html + "</div></div>";
}
bool OperationsWidgets::State::mission_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (!strict(context) || !context.component_active("away") || !action.starts_with("mission/"))
		return false;
	if (action.starts_with("mission/select/"))
	{
		mission_selected_ = action.substr(15);
		mission_picker_.clear();
		return true;
	}
	if (action == "mission/close-picker")
	{
		mission_picker_.clear();
		return true;
	}
	if (action.starts_with("mission/transport/"))
	{
		mission_selected_ = action.substr(18);
		mission_picker_ = "transport";
		return true;
	}
	if (action.starts_with("mission/slot/"))
	{
		const auto split = action.find('/', 13);
		if (split == std::string::npos)
			return false;
		mission_selected_ = action.substr(13, split - 13);
		mission_picker_ = action.substr(split + 1);
		return true;
	}
	return false;
}
std::string OperationsWidgets::State::assignment_markup(const OperationsWidgetContext &context)
{
	personnel_readouts_.clear();
	const auto board = context.board("personnel");
	if (board.empty())
		return "";
	auto line = [&](const std::string &s)
	{
		return readout(context, personnel_readouts_, s, "personnel");
	};
	const auto officers = board.at("officers");
	auto officer_tile = [&](const Json &o)
	{
		return portrait(context, o.value("portrait", std::string{}), o.at("name")) +
		       line(o.at("name").get<std::string>() + " — " + localise(o.value("condition", std::string("healthy"))) +
		            " / " + localise(o.at("location")));
	};
	std::string html = "<div class=\"personnel-columns\"><div><h2>" + localise("Posts") + "</h2>";
	for (const auto &post : board.at("duties"))
	{
		const auto key = post.at("id").get<std::string>();
		html +=
		    "<div class=\"assignment-tile\">" + context.button("assignment/post/" + key, localise(post.at("label")));
		const auto assigned = std::find_if(officers.begin(), officers.end(),
		                                   [&](const auto &o)
		                                   {
			                                   return o.at("duty") == key;
		                                   });
		html += assigned == officers.end() ? line(localise("Unassigned")) : officer_tile(*assigned);
		html += line(localise(post.at("location")));
		for (const auto &effect : post.at("effects"))
			html += line(localise(effect.at("system")) + ": " + localise(effect.at("property")) + " " +
			             effect.at("multiplier").dump());
		html += "</div>";
	}
	html += "</div><div><h2>" + localise("Away mission slots") + "</h2>";
	for (const auto &m : context.model.away_missions())
		for (const auto &slot : m.at("slots"))
		{
			const auto asset = slot.at("assigned").get<std::string>();
			const bool editable = (asset.starts_with("officer:") && slot.value("owned", false)) ||
			                      std::any_of(slot.at("choices").begin(), slot.at("choices").end(),
			                                  [](const auto &choice)
			                                  {
				                                  return choice.template get<std::string>().starts_with("officer:");
			                                  });
			const auto name = m.value("crew_names", Json::object())
			                      .value(asset, m.at("labels").value(asset, std::string("Unassigned")));
			const auto label = localise(m.at("title")) + " / " + localise(slot.at("id")) + ": " + name;
			html += "<div class=\"assignment-tile\">" +
			        portrait(context, m.value("portraits", Json::object()).value(asset, std::string{}), name);
			html += editable ? context.button("assignment/mission/" + m.at("id").get<std::string>() + "/" +
			                                      slot.at("id").get<std::string>(),
			                                  label)
			                 : line(label);
			html += "</div>";
		}
	html += "<h2>" + localise("Spare officers") + "</h2>";
	for (const auto &o : officers)
		if (o.at("duty") == "")
			html += "<div class=\"assignment-tile\">" + officer_tile(o) + "</div>";
	html += "</div></div>";
	if (!assignment_post_.empty() || !assignment_mission_.empty())
	{
		html += "<div class=\"assignment-picker\"><h2>" + localise("Choose officer") + "</h2>";
		if (!assignment_post_.empty())
		{
			for (const auto &o : officers)
				if (const auto choices = o.value("assignable_duties", Json::array());
				    std::find(choices.begin(), choices.end(), Json(assignment_post_)) != choices.end())
					html += portrait(context, o.value("portrait", std::string{}), o.at("name")) +
					        context.button("assignment/choose/" + o.at("id").get<std::string>(), o.at("name"));
		}
		else
			for (const auto &m : context.model.away_missions())
				if (m.at("id") == assignment_mission_)
					for (const auto &slot : m.at("slots"))
						if (slot.at("id") == assignment_slot_)
						{
							if (slot.value("owned", false))
								html += context.button("assignment/withdraw", localise("Withdraw assignment"));
							for (const auto &choice : slot.at("choices"))
								if (choice.get<std::string>().starts_with("officer:"))
								{
									const auto asset = choice.get<std::string>();
									const auto name = m.at("labels").value(asset, asset);
									html += portrait(context,
									                 m.value("portraits", Json::object()).value(asset, std::string{}),
									                 name) +
									        context.button("assignment/choose/" + asset.substr(8), name);
								}
						}
		html += context.button("assignment/close", localise("Close")) + "</div>";
	}
	return html + line(localise(personnel_notice_));
}
bool OperationsWidgets::State::assignment_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (!strict(context) || !context.component_active("personnel") || !action.starts_with("assignment/"))
		return false;
	if (action == "assignment/close")
	{
		assignment_post_.clear();
		assignment_mission_.clear();
		assignment_slot_.clear();
		return true;
	}
	if (action.starts_with("assignment/post/"))
	{
		assignment_post_ = action.substr(16);
		assignment_mission_.clear();
		assignment_slot_.clear();
		return true;
	}
	if (action.starts_with("assignment/mission/"))
	{
		const auto split = action.find('/', 19);
		if (split == std::string::npos)
			return false;
		assignment_mission_ = action.substr(19, split - 19);
		assignment_slot_ = action.substr(split + 1);
		assignment_post_.clear();
		return true;
	}
	if (action == "assignment/withdraw")
		return context.model.away_action("withdraw", assignment_mission_, assignment_slot_);
	if (action.starts_with("assignment/choose/"))
	{
		const auto officer = action.substr(18);
		const bool accepted =
		    !assignment_post_.empty()
		        ? context.model.personnel_action("assign", officer, assignment_post_)
		        : context.model.away_action("assign", assignment_mission_, assignment_slot_ + "/officer:" + officer);
		personnel_notice_ = accepted ? "Assignment requested" : "Assignment refused; check readiness and ownership";
		if (accepted)
		{
			assignment_post_.clear();
			assignment_mission_.clear();
			assignment_slot_.clear();
		}
		return true;
	}
	return false;
}
} // namespace phoenix
