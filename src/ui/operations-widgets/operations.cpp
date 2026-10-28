module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.operations_widgets;

import phoenix.application.preferences;
import phoenix.ui.rml;
namespace phoenix
{
struct OperationsWidgets::State
{
public:
	std::string personnel_selected_, personnel_notice_;
	std::vector<std::pair<std::string, std::string>> personnel_readouts_;
	std::string personnel_markup(const OperationsWidgetContext &context);

	bool personnel_action(const OperationsWidgetContext &context, const std::string &action);

	std::string away_notice_;
	std::vector<std::pair<std::string, std::string>> away_readouts_;

	std::string away_markup(const OperationsWidgetContext &context);

	bool away_action(const OperationsWidgetContext &context, const std::string &action);

	std::string craft_notice_;
	std::vector<std::pair<std::string, std::string>> craft_readouts_;
	std::string craft_markup(const OperationsWidgetContext &context);

	bool craft_action(const OperationsWidgetContext &context, const std::string &action);

	std::string supply_notice_;
	std::int64_t supply_quantity_ = 10;
	std::vector<std::pair<std::string, std::string>> supply_readouts_;
	std::string supply_line(const OperationsWidgetContext &context, const std::string &text);

	std::string supply_button(const OperationsWidgetContext &context, const std::string &kind,
	                          const std::string &action, const std::string &system, const std::string &text,
	                          const std::string &label);

	std::string logistics_markup(const OperationsWidgetContext &context);

	std::string docking_markup(const OperationsWidgetContext &context);

	bool supply_action(const OperationsWidgetContext &context, const std::string &action);
};
} // namespace phoenix
namespace phoenix
{
std::string OperationsWidgets::State::personnel_markup(const OperationsWidgetContext &context)
{
	personnel_readouts_.clear();
	const auto board = context.model.personnel();
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto paragraph = [&](const std::string &text, const std::string &identity)
	{
		const auto key = context.id("personnel/readout/" + identity);
		personnel_readouts_.emplace_back(key, text);
		return "<p id=\"" + key + "\">" + esc(text) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Personnel")) + "</h2><p>" +
	                   esc(localise("Select an officer, then a duty. Bonuses begin on arrival.")) + "</p>";
	if (!personnel_notice_.empty())
		html += "<p>" + esc(localise(personnel_notice_)) + "</p>";
	if (board.empty())
		return html;
	for (const auto &dep : board.at("departments"))
	{
		html += "<h2>" + esc(localise(dep.at("label"))) + "</h2>";
		const Json *selected = nullptr;
		for (const auto &o : board.at("officers"))
			if (o.at("department") == dep.at("id"))
			{
				if (o.at("id") == personnel_selected_)
					selected = &o;
				auto portrait = context.asset("../messages/console-1.png");
				const auto ref = o.at("portrait").get<std::string>();
				if (context.catalog && context.catalog->assets.contains(ref))
					portrait = register_menu_image(context.catalog->assets.at(ref));
				std::string label = (o.at("id") == personnel_selected_ ? localise("Selected") + ": " : std::string{}) +
				                    o.at("name").get<std::string>();
				label += " - " + localise(o.at("duty") == ""            ? "Reserve"
				                          : o.at("present").get<bool>() ? "On duty"
				                                                        : "Travelling");
				if (o.at("duty") != "")
					for (const auto &d : board.at("duties"))
						if (d.at("id") == o.at("duty"))
							label += ": " + localise(d.at("label"));
				for (const auto &ability : o.value("abilities", Json::array()))
					label += " / " + localise(ability.at("label"));
				label += " / " + o.at("location").get<std::string>();
				if (o.value("condition", std::string{"healthy"}) != "healthy")
					label += " / " + localise(o.at("condition")) +
					         (o.value("stabilised", false) ? " / " + localise("Stabilised") : "");
				if (o.at("remaining").get<std::int64_t>() > 0)
					label += " -> " + o.at("destination").get<std::string>();
				html += "<div class=\"personnel-row\"><img src=\"" + esc(portrait) + "\"/>" +
				        context.button("personnel/select/" + o.at("id").get<std::string>(), label);
				for (const auto &t : o.at("traits"))
				{
					std::string description = localise(t.at("label"));
					for (const auto &c : t.at("contributions"))
						description += " / " + localise(c.at("property")) + " " +
						               std::to_string(c.at("delta_permille").get<std::int64_t>() / 10) + "%";
					html += paragraph(description, "officer/" + o.at("id").get<std::string>() + "/trait/" +
					                                   t.at("id").get<std::string>());
				}
				for (const auto &[action, label] :
				     context.model.asset_away_controls("officer:" + o.at("id").get<std::string>()))
					html += context.button(action, label);
				html += "</div>";
			}
		for (const auto &d : board.at("duties"))
			if (d.at("department") == dep.at("id"))
			{
				std::string label = localise(d.at("label")) + " / " + d.at("location").get<std::string>();
				bool occupied = false;
				for (const auto &o : board.at("officers"))
					if (o.at("duty") == d.at("id"))
					{
						occupied = true;
						label += " - " + o.at("name").get<std::string>();
					}
				if (!occupied)
					label += " - " + localise("No officer");
				html += paragraph(label, "duty/" + d.at("id").get<std::string>());
				for (const auto &effect : d.at("effects"))
					html += paragraph(effect.at("system").get<std::string>() + ": " + localise(effect.at("property")) +
					                      " " + std::to_string(effect.at("multiplier").get<std::int64_t>() / 10) + "%",
					                  "duty/" + d.at("id").get<std::string>() + "/effect/" +
					                      effect.at("system").get<std::string>() + "/" +
					                      effect.at("slot").get<std::string>() + "/" +
					                      effect.at("property").get<std::string>());
				if (board.value("manual", true) && selected &&
				    (selected->at("condition") == "healthy" || selected->at("condition") == "minor") &&
				    selected->at("duty") != d.at("id"))
				{
					std::string applicable;
					for (const auto &t : selected->at("traits"))
						for (const auto &effect : d.at("effects"))
						{
							const auto slot = effect.at("slot").get<std::string>();
							for (const auto &c : t.at("contributions"))
								if (c.at("role") == slot && c.at("property") == effect.at("property"))
									applicable += localise(t.at("label")) + ": " + localise(c.at("property")) + " " +
									              std::to_string(c.at("delta_permille").get<std::int64_t>() / 10) +
									              "%; ";
						}
					html += paragraph(applicable.empty() ? localise("No applicable bonus") : applicable,
					                  "duty/" + d.at("id").get<std::string>() + "/applicable");
					html += context.button("personnel/assign/" + d.at("id").get<std::string>(),
					                       localise("Assign") + " " + selected->at("name").get<std::string>() +
					                           (occupied ? " (" + localise("relieve current officer") + ")" : ""));
					if (occupied && selected->at("duty") != "")
						html += context.button("personnel/swap/" + d.at("id").get<std::string>(), "Swap duties");
				}
			}
		if (board.value("manual", true) && selected && selected->at("duty") != "")
			html += context.button("personnel/relieve", "Relieve selected officer");
	}
	return html;
}
bool OperationsWidgets::State::personnel_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (action == "personnel")
		return context.select_page("station-page/personnel");
	if (!context.component_active("personnel") || !action.starts_with("personnel/"))
		return false;
	if (action.starts_with("personnel/select/"))
	{
		personnel_selected_ = action.substr(17);
		personnel_notice_.clear();
		return true;
	}
	bool accepted = false;
	if (action.starts_with("personnel/assign/"))
		accepted = context.model.personnel_action("assign", personnel_selected_, action.substr(17));
	if (action.starts_with("personnel/swap/"))
		accepted = context.model.personnel_action("swap", personnel_selected_, action.substr(15));
	if (action == "personnel/relieve")
		accepted = context.model.personnel_action("relieve", personnel_selected_);
	personnel_notice_ = accepted ? "Assignment requested" : "Assignment refused; refresh and try again";
	return true;
}

std::string OperationsWidgets::State::away_markup(const OperationsWidgetContext &context)
{
	away_readouts_.clear();
	auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto line = [&](const std::string &s)
	{
		const auto key = context.id("away/readout/" + std::to_string(away_readouts_.size()));
		away_readouts_.emplace_back(key, s);
		return "<p id=\"" + key + "\">" + esc(s) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Away missions")) + "</h2>" + line(localise(away_notice_));
	const auto missions = context.model.away_missions();
	if (missions.empty())
		html += line(localise("No available away missions"));
	for (const auto &m : missions)
	{
		auto label = [&](const std::string &asset)
		{
			return localise(m.at("labels").value(asset, asset));
		};
		const auto key = m.at("id").get<std::string>();
		const auto phase = m.at("phase").get<std::string>();
		html += "<div class=\"personnel-row\"><h2>" + esc(localise(m.at("title"))) + "</h2>";
		if (m.at("result") != "")
		{
			const auto result = m.at("result").get<std::string>();
			html += line(localise(result.starts_with("complete:") ? "Mission completed" : "Mission failed"));
			if (m.contains("supply") && result.find(':') != std::string::npos)
				html += line(localise(result.substr(result.find(':') + 1)));
		}
		html += line(localise(phase) + (m.at("blocker") == "" ? "" : " / " + localise(m.at("blocker"))));
		for (const auto &o : m.at("accompanying"))
			html += line(localise("Accompanying officer") + ": " + label("officer:" + o.get<std::string>()));
		if (m.contains("boarding_check"))
		{
			const auto &c = m.at("boarding_check");
			const auto percent = [](std::int64_t value)
			{
				const auto magnitude = value < 0 ? -value : value;
				return std::string(value < 0 ? "-" : "") + std::to_string(magnitude / 10) +
				       (magnitude % 10 ? "." + std::to_string(magnitude % 10) : "") + "%";
			};
			html += line(localise("Encounter chance") + ": " + percent(c.at("chance_permille").get<int>()) + " / " +
			             localise("Defending teams") + ": " + std::to_string(c.at("defenders").get<int>()));
			for (const auto &[key, value] : c.at("factors").items())
				html += line(localise(key) + ": " + percent(value.get<std::int64_t>()));
		}
		html += line(localise("Transport") + ": " + label(m.at("transport").get<std::string>()));
		html += line(localise("Destination contact") + " #" + std::to_string(m.value("target", std::uint64_t{0})));
		if (m.value("ship_transport_reason", std::string{}) != "")
			html += line(localise(m.at("ship_transport_reason")));
		auto button = [&](const std::string &action, const std::string &value, const std::string &label)
		{
			return context.button("away/" + action + "/" + key + "/" + value, label);
		};
		html += line(localise("Dispatch") + ": " + localise(m.value("owner", std::string{})));
		if (m.value("can_contribute", false))
			html += button("contribute", m.value("contributing", false) ? "0" : "1",
			               m.value("contributing", false) ? "Withdraw department contribution"
			                                              : "Authorise department contribution");
		if (m.contains("supply"))
		{
			const auto &s = m.at("supply");
			html += line(localise(s.at("material")) + ": " + std::to_string(s.at("quantity").get<std::int64_t>()) +
			             " / " + localise(s.at("phase")));
			html += line(localise("Payment") + ": " + localise(s.at("payment")) + " " +
			             std::to_string(s.at("payment_quantity").get<std::int64_t>()));
			html += line(localise("Delivered") + ": " + std::to_string(s.at("delivered").get<std::int64_t>()));
			if (s.at("manual").get<bool>())
				for (const auto &q : s.at("quantities"))
					html += button("quantity", std::to_string(q.get<std::int64_t>()),
					               q == 0 ? localise("Automatic manifest")
					                      : localise("Collect") + " " + std::to_string(q.get<std::int64_t>()));
		}
		for (const auto &s : m.value("craft_slots", Json::array()))
		{
			html += line(localise(s.at("id")) + ": " + label(s.at("assigned").get<std::string>()));
			if (s.value("lost", false))
				html += line(localise("Escort lost"));
			if (s.at("owned").get<bool>() && m.value("can_contribute", false))
				html += button("escort", s.at("id").get<std::string>() + "/", "Withdraw escort");
			for (const auto &choice : s.at("choices"))
				html += button("escort", s.at("id").get<std::string>() + "/" + choice.get<std::string>(),
				               localise("Assign escort") + ": " + label(choice.get<std::string>()));
		}
		if (m.value("transport_owned", false))
			html += button("transport", "", "Withdraw transport");
		if (m.at("dispatch").get<bool>())
		{
			if (phase == "available")
				html +=
				    button("open", "", m.value("automatic_dispatch", false) ? "Activate mission" : "Open assignments");
			if ((phase == "recovered" || (phase == "open" && !m.value("automatic_dispatch", false))) &&
			    m.at("result") == "")
				html += button("send", "", "Send");
			if (phase == "deployed")
				html += button("recall", "", "Recall");
			if (phase == "open" || phase == "outbound" || phase == "inbound")
				html += button("cancel", "", "Cancel transfer");
		}
		if ((phase == "open" || phase == "recovered" || phase == "deployed") &&
		    (m.at("result") == "" || phase == "deployed"))
			for (const auto &t : m.at("transports"))
				html += button("transport", t, localise("Assign transport") + ": " + label(t.get<std::string>()));
		for (const auto &s : m.at("slots"))
		{
			const auto slot = s.at("id").get<std::string>();
			html += line(slot + " (" + localise(s.at("required").get<bool>() ? "Required" : "Optional") +
			             "): " + label(s.at("assigned").get<std::string>()));
			if ((phase == "open" || phase == "recovered") && s.at("owned").get<bool>())
				html += button("withdraw", slot, "Withdraw assignment");
			if (s.at("assigned") == "")
				for (const auto &c : s.at("choices"))
					html += button("assign", slot + "/" + c.get<std::string>(),
					               localise("Assign") + ": " + label(c.get<std::string>()));
		}
		html += "</div>";
	}
	return html;
}
bool OperationsWidgets::State::away_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (action == "away")
		return context.select_page("station-page/away");
	if (!context.component_active("away") || !action.starts_with("away/"))
		return false;
	const auto a = action.find('/', 5), b = action.find('/', a + 1);
	if (a == std::string::npos || b == std::string::npos)
		return false;
	away_notice_ =
	    context.model.away_action(action.substr(5, a - 5), action.substr(a + 1, b - a - 1), action.substr(b + 1))
	        ? "Mission command accepted"
	        : "Mission command refused; check readiness and ownership";
	return true;
}
std::string OperationsWidgets::State::craft_markup(const OperationsWidgetContext &context)
{
	craft_readouts_.clear();
	auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	auto line = [&](const std::string &s)
	{
		const auto key = context.id("craft/readout/" + std::to_string(craft_readouts_.size()));
		craft_readouts_.emplace_back(key, s);
		return "<p id=\"" + key + "\">" + esc(s) + "</p>";
	};
	std::string html = "<h2>" + esc(localise("Flight operations")) + "</h2>" + line(localise(craft_notice_));
	const auto rows = context.model.craft();
	if (rows.empty())
		html += line(localise("No craft under this Console"));
	for (const auto &c : rows)
	{
		const auto craft = c.at("id").get<std::string>();
		html += "<div class=\"personnel-row\"><h2>" +
		        esc(c.at("bay").get<std::string>() + " / " + localise(c.at("role")) + " #" + craft) + "</h2>";
		html += line(localise(c.at("phase")) + " / " + localise(c.at("order")));
		if (c.value("blocker", std::string{}) != "")
			html += line(localise(c.at("blocker")));
		html += line(localise("Energy") + ": " + std::to_string(c.at("energy").get<std::int64_t>()) + "/" +
		             std::to_string(c.at("capacity").get<std::int64_t>()) + " / " + localise("Ammunition") + ": " +
		             std::to_string(c.at("ammunition").get<std::int64_t>()));
		html += line(localise("Condition") + ": " + std::to_string(c.at("condition").get<std::int64_t>()) + "/" +
		             std::to_string(c.at("maximum_condition").get<std::int64_t>()));
		if (!c.at("linked").get<bool>())
			html += line(localise("Link unavailable; showing last report") + " " +
			             std::to_string(c.at("report_tick").get<std::uint64_t>()));
		if (c.at("pending").get<bool>())
			html += line(localise("Order pending delivery"));
		auto button = [&](const std::string &action, const std::string &target, const std::string &label)
		{
			if (!c.value("manual", true))
				return std::string{};
			return context.button("craft/" + action + "/" + craft + "/" + target, label);
		};
		if (c.at("phase") != "lost" && c.at("phase") != "consumed" &&
		    (c.at("role") != "boarding_torpedo" || c.at("phase") == "launching") &&
		    (c.at("phase") != "docked" || !c.at("available").get<bool>()))
			html += button("recall", "", "Recall");
		if (c.at("role") == "interceptor")
		{
			html += button("defend", "", "Defend carrier");
			if (context.model.observation())
				html += button("escort", std::to_string(context.model.observation()->own.id), "Escort own ship");
		}
		if (context.model.observation())
			for (const auto &target : context.model.observation()->contacts)
				if (target.id < 1000000000ULL && std::to_string(target.id) != craft)
				{
					const auto name = localise(target.definition) + " #" + std::to_string(target.id);
					if (c.at("role") == "interceptor")
						html += button("escort", std::to_string(target.id), localise("Escort") + ": " + name);
					if (c.at("role") == "bomber" && target.disposition == "hostile")
						html += button("bomb", std::to_string(target.id), localise("Bomb target") + ": " + name);
				}
		html += "</div>";
	}
	return html;
}
bool OperationsWidgets::State::craft_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (!context.model.ui_availability().features.contains("craft"))
		return false;
	if (action == "craft")
		return context.select_page("station-page/craft");
	if (!context.component_active("craft") || !action.starts_with("craft/"))
		return false;
	const auto a = action.find('/', 6), b = action.find('/', a + 1);
	if (a == std::string::npos || b == std::string::npos)
		return false;
	craft_notice_ =
	    context.model.craft_action(action.substr(6, a - 6), action.substr(a + 1, b - a - 1), action.substr(b + 1))
	        ? "Flight order accepted"
	        : "Flight order refused; check readiness and ownership";
	return true;
}
std::string OperationsWidgets::State::supply_line(const OperationsWidgetContext &context, const std::string &text)
{
	const auto key = context.id(context.component + "/supply/readout/" + std::to_string(supply_readouts_.size()));
	supply_readouts_.emplace_back(key, text);
	return "<p id=\"" + key + "\">" + Rml::StringUtilities::EncodeRml(text) + "</p>";
}
std::string OperationsWidgets::State::supply_button(const OperationsWidgetContext &context, const std::string &kind,
                                                    const std::string &action, const std::string &system,
                                                    const std::string &text, const std::string &label)
{
	return context.button("supply/" + kind + "/" + action + "/" + system + "/" + text, label);
}
std::string OperationsWidgets::State::logistics_markup(const OperationsWidgetContext &context)
{
	supply_readouts_.clear();
	std::string html =
	    "<h2>" + localise("Stores and deliveries") + "</h2>" + supply_line(context, localise(supply_notice_));
	const auto board = context.model.logistics();
	const bool manual = board.value("manual", true);
	if (manual)
	{
		html += supply_line(context, localise("Transfer quantity") + ": " + std::to_string(supply_quantity_));
		html += context.button("supply-quantity/down", "Reduce transfer quantity");
		html += context.button("supply-quantity/up", "Increase transfer quantity");
	}
	for (const auto &s : board.value("stores", Json::array()))
	{
		bool controllable = false;
		if (context.model.observation())
			for (const auto &ctrl : context.model.observation()->gameplay.at("controllers"))
				controllable |= ctrl.at("id") == s.at("controller") && ctrl.value("human", false);
		const auto store = s.at("id").get<std::string>();
		const auto resource_label = [&](const std::string &id)
		{
			return localise(s.value("resource_labels", Json::object()).value(id, id));
		};
		html += supply_line(context,
		                    localise(store) + (s.at("working").get<bool>() ? "" : " / " + localise("Unavailable")));
		if (controllable && s.at("main").get<bool>())
			html += supply_button(context, "logistics", s.at("held").get<bool>() ? "release" : "hold", store, "",
			                      s.at("held").get<bool>() ? "Release stock" : "Hold stock");
		if (s.value("single_resource", false))
		{
			html += supply_line(context, localise("Selected resource") + ": " +
			                                 resource_label(s.value("selected_resource", std::string{})));
			for (const auto &resource : s.value("resource_choices", Json::array()))
			{
				const auto choice = resource.get<std::string>();
				const auto choices = s.value("selectable_resources", Json::array());
				if (manual && controllable && s.value("selected_resource", std::string{}) != choice &&
				    std::find(choices.begin(), choices.end(), resource) != choices.end())
					html += supply_button(context, "logistics", "select", store, choice,
					                      localise("Select resource") + ": " + resource_label(choice));
				else
					html += supply_line(context, resource_label(choice));
			}
		}
		for (const auto &[m, q] : s.at("quantities").items())
		{
			const auto target = s.at("targets").value(m, std::int64_t{0});
			html += supply_line(context, localise(m) + ": " + q.dump() + " / " + localise("Target") + " " +
			                                 std::to_string(target) + " / " + localise("Receiving space") + " " +
			                                 s.at("space").at(m).dump());
			if (controllable)
				html += supply_button(context, "logistics", "target", store,
				                      m + "/" + std::to_string(std::max<std::int64_t>(0, target - 1)),
				                      localise("Reduce target") + " " + localise(m));
			if (controllable)
				html += supply_button(context, "logistics", "target", store, m + "/" + std::to_string(target + 1),
				                      localise("Increase target") + " " + localise(m));
		}
	}
	if (context.model.observation())
	{
		const auto &gameplay = context.model.observation()->gameplay;
		const auto controllable = [&](const std::string &equipment)
		{
			for (const auto &controller : gameplay.value("controllers", Json::array()))
				if (controller.value("human", false))
					for (const auto &channel : controller.at("commands"))
						for (const auto &consumer : channel.at("consumers"))
							if (consumer == equipment)
								return true;
			return false;
		};
		for (const auto &magazine : gameplay.value("combat", Json::object()).value("magazines", Json::array()))
		{
			const auto system = magazine.at("id").get<std::string>();
			const auto resource_label = [&](const std::string &id)
			{
				return localise(magazine.value("resource_labels", Json::object()).value(id, id));
			};
			html += supply_line(context, localise(system) + " / " + localise("Selected resource") + ": " +
			                                 resource_label(magazine.at("selected_resource")));
			const auto selectable = magazine.value("selectable_resources", Json::array());
			for (const auto &resource : magazine.at("resource_choices"))
			{
				const auto choice = resource.get<std::string>();
				if (magazine.value("manual", false) && controllable(system) &&
				    resource != magazine.at("selected_resource") &&
				    std::find(selectable.begin(), selectable.end(), resource) != selectable.end())
					html += supply_button(context, "magazine", "select", system, choice,
					                      localise("Select resource") + ": " + resource_label(choice));
				else
					html += supply_line(context, resource_label(choice));
			}
		}
		for (const auto &processor : gameplay.value("processors", Json::array()))
		{
			const auto system = processor.at("id").get<std::string>();
			std::string selected_label = processor.at("recipe");
			for (const auto &recipe : processor.at("recipes"))
				if (recipe.at("id") == processor.at("recipe"))
					selected_label = recipe.at("label");
			html += supply_line(context, localise(system) + " / " + localise("Selected recipe") + ": " +
			                                 localise(selected_label) + " / " +
			                                 localise(processor.at("active").get<bool>() ? "Processing" : "Idle") +
			                                 " / " + localise("Progress") + ": " + processor.at("progress").dump() +
			                                 "/" + processor.at("work").dump());
			for (const auto &recipe : processor.at("recipes"))
			{
				const auto choice = recipe.at("id").get<std::string>();
				const auto label = localise(recipe.at("label"));
				if (processor.value("manual", false) && controllable(processor.at("equipment")) &&
				    choice != processor.at("recipe").get<std::string>())
					html += supply_button(context, "processor", "select", system, choice,
					                      localise("Select recipe") + ": " + label);
				else
					html += supply_line(context, label);
			}
		}
	}
	for (const auto &r : board.value("requests", Json::array()))
	{
		html += supply_line(context, localise("Waiting delivery") + " #" + r.at("id").dump() + ": " +
		                                 r.at("material").get<std::string>() + " " + r.at("remaining").dump());
		if (manual)
			html += supply_button(context, "logistics", "cancel", r.at("id").dump(), "", "Cancel waiting delivery");
	}
	for (const auto &b : board.value("batches", Json::array()))
	{
		html += supply_line(context, localise("In transit") + " #" + b.at("id").dump() + ": " +
		                                 b.at("material").get<std::string>() + " " + b.at("quantity").dump() + " / " +
		                                 localise("Location") + " " + b.at("location").dump() + ":" +
		                                 b.at("compartment").dump());
		if (manual)
			html += supply_button(context, "logistics", "cancel", "batch:" + b.at("id").dump(), "", "Return delivery");
	}
	bool engineering_control = false;
	if (context.model.observation())
		for (const auto &ctrl : context.model.observation()->gameplay.at("controllers"))
			engineering_control |= ctrl.at("id") == board.value("controller", Json("")) && ctrl.value("human", false);
	bool route_control = false;
	if (context.model.observation())
		for (const auto &ctrl : context.model.observation()->gameplay.at("controllers"))
			route_control |= ctrl.at("id") == board.value("route_controller", Json("")) && ctrl.value("human", false);
	for (const auto &route : board.value("routes", Json::array()))
	{
		const auto from = route.at("from").get<std::string>(), to = route.at("to").get<std::string>();
		const bool closed = route.at("closed").get<bool>();
		html +=
		    supply_line(context, localise(from) + " → " + localise(to) + " / " + localise(closed ? "Closed" : "Open"));
		if (route_control)
			html += supply_button(context, "logistics", "route", from, to + (closed ? "/open" : "/close"),
			                      localise(closed ? "Open route" : "Close route") + ": " + localise(from) + " → " +
			                          localise(to));
	}
	for (const auto &craft : board.value("cargo_craft", Json::array()))
	{
		html += supply_line(context,
		                    localise("Supply shuttle") + " #" + craft.at("id").dump() + " / " +
		                        localise(craft.at("authorised").get<bool>() ? "Authorised" : "Permission required"));
		if (craft.at("controllable").get<bool>())
			html += supply_button(context, "logistics", "craft_allow", craft.at("id").dump(),
			                      craft.at("authorised").get<bool>() ? "0" : "1",
			                      craft.at("authorised").get<bool>() ? "Revoke supply permission"
			                                                         : "Authorise cargo sorties");
	}
	for (const auto &offer : board.value("offers", Json::array()))
		if (manual)
			for (const auto &store : board.value("stores", Json::array()))
			{
				const auto material = offer.at("material").get<std::string>(),
				           source = store.at("id").get<std::string>();
				if (!store.at("quantities").contains(material) ||
				    store.at("quantities").at(material).get<std::int64_t>() < 1)
					continue;
				const auto destination =
				    offer.at("entity").dump() + "/" + offer.at("store").get<std::string>() + "/" + material + "/" +
				    std::to_string(std::min(supply_quantity_, store.at("quantities").at(material).get<std::int64_t>()));
				if (offer.at("connected").get<bool>())
					html += supply_button(context, "logistics", "transfer", source, destination,
					                      localise("Send supply") + ": " + localise(material) + " → #" +
					                          offer.at("entity").dump());
				for (const auto &craft : board.value("cargo_craft", Json::array()))
					if (craft.at("authorised").get<bool>() && craft.at("available").get<bool>())
						for (const auto &cargo : craft.at("stores"))
						{
							bool accepts = false;
							for (const auto &type : cargo.at("accepts"))
								accepts |= type == material;
							if (!accepts)
								continue;
							const auto text =
							    craft.at("id").dump() + "/" + cargo.at("id").get<std::string>() + "/" + destination;
							html += supply_button(context, "logistics", "deliver", source, text,
							                      localise("Deliver by shuttle") + ": " + localise(material) + " → #" +
							                          offer.at("entity").dump());
						}
			}
	for (const auto &job : board.value("cargo", Json::array()))
	{
		html += supply_line(context, localise("Cargo delivery") + " #" + job.at("id").dump() + ": " +
		                                 localise(job.at("phase")) + " / " + localise("Report tick") + " " +
		                                 job.at("report_tick").dump());
		if (job.at("phase") != "complete" && job.at("phase") != "lost")
			html +=
			    supply_button(context, "logistics", "cargo_cancel", job.at("id").dump(), "", "Cancel cargo delivery");
	}

	return html;
}
std::string OperationsWidgets::State::docking_markup(const OperationsWidgetContext &context)
{
	supply_readouts_.clear();
	std::string html = "<h2>" + localise("Docking") + "</h2>" + supply_line(context, localise(supply_notice_));
	const auto board = context.model.docking();
	for (const auto &p : board.value("ports", Json::array()))
	{
		const auto port = p.at("id").get<std::string>();
		const auto phase = p.at("phase").get<std::string>();
		html += supply_line(context, localise(port) + " / " + localise(phase) +
		                                 (p.at("working").get<bool>() ? "" : " / " + localise("Unavailable")));
		html += supply_button(context, "dock", p.at("accepting").get<bool>() ? "reject" : "accept", port, "0",
		                      p.at("accepting").get<bool>() ? "Close allied docking" : "Allow allied docking");
		html += supply_button(context, "dock", "supply", port, p.at("supply").get<bool>() ? "0" : "1",
		                      p.at("supply").get<bool>() ? "Stop supply service" : "Authorise supply service");
		if (phase != "free")
		{
			html += supply_line(context, localise("Connected entity") + ": " + p.at("target").dump());
			html += supply_button(context, "dock", phase == "approach" ? "cancel" : "undock", port, "",
			                      phase == "approach" ? "Cancel approach" : "Undock");
			if (p.at("emergency").get<bool>())
				html += supply_button(context, "dock", "emergency", port, "", "Emergency release");
		}
		else
			for (const auto &t : board.value("targets", Json::array()))
				if (t.at("compatibility") == p.at("compatibility") && t.at("available").get<bool>())
				{
					const auto target = t.at("entity").dump() + "/" + t.at("port").get<std::string>();
					html += supply_button(context, "dock", "assist", port, target,
					                      localise("Assisted approach") + " #" + t.at("entity").dump() + " / " +
					                          t.at("port").get<std::string>());
					html += supply_button(context, "dock", "connect", port, target,
					                      localise("Dock manually") + " #" + t.at("entity").dump());
				}
	}
	return html;
}
bool OperationsWidgets::State::supply_action(const OperationsWidgetContext &context, const std::string &action)
{
	if (context.component_active("operations") && action.starts_with("supply-quantity/"))
	{
		supply_quantity_ = std::clamp<std::int64_t>(supply_quantity_ + (action.ends_with("up") ? 10 : -10), 1, 1000000);
		return true;
	}
	if ((!context.component_active("operations") && !context.component_active("docking")) ||
	    !action.starts_with("supply/"))
		return false;
	const auto a = action.find('/', 7), b = action.find('/', a + 1), c = action.find('/', b + 1);
	if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
		return false;
	const auto kind = action.substr(7, a - 7), verb = action.substr(a + 1, b - a - 1),
	           system = action.substr(b + 1, c - b - 1), text = action.substr(c + 1);
	if (!context.component_active(kind == "dock" ? "docking" : "operations"))
		return false;
	const bool ok = kind == "dock"                              ? context.model.docking_action(verb, system, text)
	                : kind == "magazine" || kind == "processor" ? context.model.selection_action(kind, system, text)
	                                                            : context.model.logistics_action(verb, system, text);
	supply_notice_ = ok ? "Order accepted" : "Order refused; check readiness and ownership";
	return true;
}
OperationsWidgets::OperationsWidgets() : state_(std::make_unique<State>())
{
}
OperationsWidgets::~OperationsWidgets() = default;
WidgetPanel OperationsWidgets::render(const std::string &component, OperationsWidgetContext context)
{
	context.component = component;
	if (component == "operations")
		return {state_->logistics_markup(context), state_->supply_readouts_};
	if (component == "docking")
		return {state_->docking_markup(context), state_->supply_readouts_};
	if (component == "craft")
		return {state_->craft_markup(context), state_->craft_readouts_};
	if (component == "personnel")
		return {state_->personnel_markup(context), state_->personnel_readouts_};
	if (component == "away")
		return {state_->away_markup(context), state_->away_readouts_};
	return {};
}
bool OperationsWidgets::action(const OperationsWidgetContext &context, const std::string &action)
{
	return state_->away_action(context, action) || state_->supply_action(context, action) ||
	       state_->craft_action(context, action) || state_->personnel_action(context, action);
}
bool OperationsWidgets::away_attention(const ConsoleModel &model) const
{
	for (const auto &m : model.away_missions())
		for (const auto &s : m.at("slots"))
			if (s.at("assigned") == "" && !s.at("choices").empty())
				return true;
	return false;
}
} // namespace phoenix
