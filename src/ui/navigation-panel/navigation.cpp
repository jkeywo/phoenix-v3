module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.navigation_panel;
import phoenix.graphics.radar;
import phoenix.application.preferences;
namespace phoenix
{
void NavigationPanel::prepare(ConsoleModel &model_, NavigationFrame frame)
{
	auto &dom_ = frame.dom;
	auto *document_ = &dom_.document();
	auto *context_ = &frame.context;
	const auto &region = frame.region;
	const bool pregame = frame.pregame;

	navigation_visible_ = false;
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			navigation_visible_ |=
			    widget.at("kind") == "navigation" && ui_widget_available(widget, model_.ui_availability());
	const auto objectives = model_.objectives();
	const auto report =
	    model_.observation() ? model_.observation()->gameplay.value("mission_report", Json(nullptr)) : Json(nullptr);
	const auto ftl =
	    model_.observation() && !pregame ? model_.observation()->gameplay.value("ftl", Json::array()) : Json::array();
	const bool navigator_panel = !ftl.empty() && model_.layout() && model_.layout()->at("widgets").empty() &&
	                             !model_.layout()->contains("layout");
	if (const auto *layout = model_.layout(); layout && !pregame)
		for (const auto &widget : layout->at("widgets"))
			if (widget.at("kind") == "objectives")
				if (auto *target = document_->GetElementById("value/" + widget.at("id").get<std::string>()))
				{
					std::string text;
					for (const auto &objective : objectives)
						text += "<p>" +
						        Rml::StringUtilities::EncodeRml(localise(objective.value("title", "")) + ": " +
						                                        localise(objective.value("outcome", "").empty()
						                                                     ? objective.value("status", "")
						                                                     : objective.value("outcome", "")) +
						                                        ". " + localise(objective.value("description", ""))) +
						        "</p>";
					dom_.leaf_markup(target, text);
				}
	mission_outcome_.clear();
	for (const auto &objective : objectives)
		if (!objective.value("outcome", "").empty())
			mission_outcome_ = objective.at("outcome");
	if (!report.is_null())
		mission_outcome_ = "ftl";
	if (pregame)
		mission_outcome_.clear();
	if (!mission_outcome_.empty())
		navigation_visible_ = false;
	document_->SetClass("navigation-console", navigation_visible_);
	if ((navigation_visible_ || navigator_panel || !mission_outcome_.empty()) &&
	    (!(model_.layout() && model_.layout()->contains("layout")) || !mission_outcome_.empty()))
		dom_.property(document_->GetElementById("gameplay"), "display", "none");
	navigation_area_ = {0, 110, region.width, std::max(50.f, region.height - 250)};
	if (navigation_visible_ && model_.observation())
	{
		const auto &own = model_.observation()->own;
		double left = own.x, right = own.x, bottom = own.y, top = own.y;
		auto include = [&](double x, double y, double radius)
		{
			left = std::min(left, x - radius);
			right = std::max(right, x + radius);
			bottom = std::min(bottom, y - radius);
			top = std::max(top, y + radius);
		};
		for (const auto &contact : model_.observation()->contacts)
			include(contact.x, contact.y, 0);
		const auto w = model_.waypoint();
		if (w.value("active", false))
			include(w.at("x"), w.at("y"), 0);
		navigation_camera_.fit(left, bottom, right, top, navigation_area_.width, navigation_area_.height);
	}
	std::vector<std::pair<std::string, std::string>> actions;
	auto button = [&](std::string key, std::string label)
	{
		actions.emplace_back(key, localise(label));
		if (key.starts_with("on-screen/"))
			return frame.share_markup(key);
		return "<button id=\"" + frame.id(key) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
	};
	std::string markup;
	if (!mission_outcome_.empty())
	{
		std::erase_if(frame.output.controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(frame.id(""));
		              });
		std::erase_if(frame.output.inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(frame.id(""));
		              });
		markup =
		    "<div class=\"mission-result\" style=\"width:" + std::to_string(region.width) +
		    "px;height:" + std::to_string(region.height) + "px;\"><img class=\"mission-art\" src=\"" +
		    frame.asset("mission-terminal.png") +
		    "\"/><div class=\"mission-copy\" style=\"left:" + std::to_string(region.width * .07f) +
		    "px;top:" + std::to_string(region.height * .08f) + "px;width:" + std::to_string(region.width * .86f) +
		    "px;height:" + std::to_string(region.height * .8f) + "px;\"><h1>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("title").get<std::string>()
		                                                                  : "MISSION REPORT // FINAL") +
		    "</h1><h2>" +
		    localise(!report.is_null() && report.value("version", 1) == 2 ? report.at("result_title").get<std::string>()
		             : mission_outcome_ == "ftl"                          ? "Departed via FTL"
		             : mission_outcome_ == "won"                          ? "You won"
		             : mission_outcome_ == "lost"                         ? "You lost"
		                                                                  : "Draw") +
		    "</h2><div class=\"terminal-rule\"></div><p class=\"terminal-status\">" + localise("Mission complete") +
		    "</p>";
		std::string report_text = localise(mission_outcome_ == "ftl" ? "Departed via FTL" : "Mission complete");
		if (!report.is_null() && report.value("version", 1) == 2)
			report_text += ". " + localise(report.at("title")) + ". " + localise(report.at("result_title"));
		const auto column_height = std::to_string(region.height * .57f) + "px";
		markup += "<div style=\"display:flex;width:" + std::to_string(region.width * .81f) +
		          "px;gap:24px;\"><div class=\"mission-report-main\" style=\"flex-shrink:0;width:" +
		          std::to_string(region.width * .53f) + "px;height:" + column_height + ";overflow:auto;\">";
		for (const auto &o : report.is_null() ? objectives : report.at("objectives"))
		{
			const auto status = o.value("status", "active");
			const auto line =
			    localise(o.value("title", "")) +
			    (report.is_null() ? ""
			                      : ": " + localise((status == "active" || status == "pending") ? "Unfinished"
			                                        : status == "completed"                     ? "Completed"
			                                        : status == "failed"                        ? "Failed"
			                                                                                    : "Cancelled"));
			markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
			report_text += ". " + line;
		}
		if (!report.is_null() && report.value("version", 1) == 2)
		{
			auto line = [&](std::string value)
			{
				markup += "<p>" + Rml::StringUtilities::EncodeRml(value) + "</p>";
				report_text += ". " + value;
			};

			line(localise(report.at("explanation")));
			for (const auto &a : report.value("assessments", Json::array()))
			{
				line(localise(a.at("title")) + ": " + localise(a.at("result_title")));
				line(localise(a.at("explanation")));
			}
			markup += "<h1>" + localise("Mission log") + "</h1>";
			for (const auto &entry : report.value("log", Json::array()))
			{
				const auto seconds = entry.at("tick").get<std::uint64_t>() / 20;
				line(std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
				     std::to_string(seconds % 60) + " — " + localise(entry.at("text")) +
				     (entry.value("name", "").empty() ? "" : ": " + localise(entry.at("name"))));
			}
			markup += "</div><div class=\"mission-report-side\" style=\"flex-shrink:0;width:" +
			          std::to_string(region.width * .25f) + "px;height:" + column_height +
			          ";overflow:auto;border-left:1px #51744c;padding-left:16px;\"><h1>" + localise("Ship and crew") +
			          "</h1>";
			for (const auto &part : report.value("ship_condition", Json::array()))
				line(localise(part.at("name")) + ": " + part.at("hp").dump() + "/" + part.at("maximum").dump());
			for (const auto &crew : report.value("crew", Json::array()))
				line(localise(crew.at("name")) + ": " + localise(crew.at("condition")));
			for (const auto &f : report.at("facts"))
			{
				std::string value = f.at("value").is_null()      ? localise("Unresolved")
				                    : f.at("value").is_string()  ? localise(f.at("value").get<std::string>())
				                    : f.at("value").is_boolean() ? localise(f.at("value").get<bool>() ? "Yes" : "No")
				                                                 : f.at("value").dump();
				line(localise(f.at("label")) + ": " + value);
			}
			for (const auto &p : report.at("personnel"))
				line(localise(p.at("name")) + ": " + localise(p.at("status")) +
				     (p.value("location", "").empty() ? "" : " — " + localise(p.at("location"))));
		}
		if (!report.is_null() && report.value("version", 1) == 1)
			for (const auto &name : report.at("left_behind"))
			{
				const auto line = localise("Personnel left behind") + ": " + localise(name);
				markup += "<p>" + Rml::StringUtilities::EncodeRml(line) + "</p>";
				report_text += ". " + line;
			}
		markup += "</div></div>" + button("mission.leave", "Return to menu") + "</div></div>";
		frame.output.readouts.emplace_back(frame.id("mission-result"), report_text);
	}
	else if (navigator_panel)
	{
		std::erase_if(frame.output.controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with(frame.id("game."));
		              });
		std::erase_if(frame.output.inputs,
		              [&](const auto &input)
		              {
			              return input.id.starts_with(frame.id("game."));
		              });
		markup = "<div class=\"navigator-panel\" style=\"position:absolute;left:5%;top:120px;width:90%;height:" +
		         std::to_string(std::max(50.f, region.height - 160)) + "px;overflow:auto;\"><h1>" +
		         localise("FTL departure") + "</h1>";
		for (const auto &[key, text] : model_.gameplay_readouts())
			if (key.starts_with("game.readout/ftl"))
				markup += "<p>" + Rml::StringUtilities::EncodeRml(text) + "</p>";
		for (const auto &[key, label] : model_.gameplay_controls())
			if (key.starts_with("game.control/"))
			{
				actions.emplace_back(key, label);
				markup +=
				    "<button id=\"" + frame.id(key) + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
		if (!ftl.at(0).at("ready").get<bool>())
			markup += "<button disabled=\"disabled\" aria-disabled=\"true\">" + localise("Leave mission") + "</button>";
		markup += "</div>";
	}
	else if (navigation_visible_)
	{
		markup = "<div class=\"navigation-canvas\" id=\"" + frame.id("nav.map") +
		         "\" tabindex=\"0\" aria-label=\"Navigation map. Arrow keys select a "
		         "position; Page Up and Page Down zoom; Tab reaches the waypoint action.\" "
		         "style=\"position:absolute;left:0;top:0;width:100%;height:88%;\"></div><div "
		         "class=\"navigation-controls\" style=\"position:absolute;left:0;bottom:0;width:100%;height:12%;\">";
		if (navigation_selected_contact_ && model_.observation())
			for (const auto &contact : model_.observation()->contacts)
				if (contact.id == navigation_selected_contact_)
					markup += "<span>" +
					          Rml::StringUtilities::EncodeRml(localise(contact.definition) + " #" +
					                                          std::to_string(contact.id)) +
					          "</span>";
		if (navigation_selected_)
		{
			const auto waypoint = model_.waypoint();
			const bool selected_waypoint =
			    waypoint.value("active", false) &&
			    std::hypot(navigation_x_ - waypoint.value("x", 0.0), navigation_y_ - waypoint.value("y", 0.0)) <=
			        navigation_camera_.scale * 18;
			markup += button(selected_waypoint ? "nav.clear" : "nav.set",
			                 selected_waypoint ? "Clear waypoint" : "Set waypoint");
		}
		if (const auto *layout = model_.layout())
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("kind") == "navigation")
					markup += button("on-screen/" + widget.at("id").get<std::string>(), "On screen");
		markup += "</div>";
	}
	if (markup != navigation_markup_)
	{
		const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("navigation-layer")->SetInnerRML(markup);
		// Size the report after insertion; this layer has no intrinsic layout width.
		if (!mission_outcome_.empty())
			if (auto *copy = document_->QuerySelector(".mission-copy"))
			{
				auto *layer = document_->GetElementById("navigation-layer");
				layer->SetProperty("display", "block");
				layer->SetProperty("width", std::to_string(region.width) + "px");
				layer->SetProperty("height", std::to_string(region.height) + "px");
				auto *pane = copy->GetParentNode();
				pane->SetProperty("display", "block");
				pane->SetProperty("width", std::to_string(region.width) + "px");
				pane->SetProperty("height", std::to_string(region.height) + "px");
				copy->SetProperty("display", "block");
				copy->SetProperty("position", "absolute");
				copy->SetProperty("left", std::to_string(region.width * .07f) + "px");
				copy->SetProperty("top", std::to_string(region.height * .08f) + "px");
				copy->SetProperty("width", std::to_string(region.width * .86f) + "px");
				copy->SetProperty("height", std::to_string(region.height * .8f) + "px");
				for (int child = 0; child < copy->GetNumChildren(); ++child)
				{
					auto *line = copy->GetChild(child);
					if (line->GetTagName() == "h1" || line->GetTagName() == "h2" || line->GetTagName() == "p")
						line->SetProperty("width", std::to_string(region.width * .86f) + "px");
				}
				for (const auto &[selector, fraction] : std::vector<std::pair<std::string, float>>{
				         {".mission-report-main", .53f}, {".mission-report-side", .25f}})
					if (auto *column = copy->QuerySelector(selector))
						for (int child = 0; child < column->GetNumChildren(); ++child)
							column->GetChild(child)->SetProperty("width",
							                                     std::to_string(region.width * fraction - 24) + "px");
			}

		navigation_markup_ = markup;
		dom_.forget_elements();
		if (!mission_outcome_.empty())
		{
			frame.accessible_status = mission_outcome_ == "ftl"
			                              ? localise("Departed via FTL")
			                              : localise("Mission complete") + ": " + localise(mission_outcome_);
			if (auto *button = document_->GetElementById(frame.id("mission.leave")))
				button->Focus();
		}
		else if (auto *previous = document_->GetElementById(focus))
			previous->Focus();
	}
	for (const auto &[key, label] : actions)
		if (key.starts_with("on-screen/"))
			if (auto *b = document_->GetElementById(frame.id(key)))
			{
				const bool active = model_.is_on_screen(key.substr(10));
				frame.output.toggles[frame.id(key)] = active;
				b->SetClass("on-screen-active", active);
				b->SetAttribute("aria-pressed", active ? "true" : "false");
			}

	context_->Update();
	navigation_buttons_.clear();
	for (const auto &[key, label] : actions)
	{
		frame.output.controls.emplace_back(frame.id(key), label);
		frame.output.place(dom_, frame.context, region, frame.id(key));
		if (auto *button = document_->GetElementById(frame.id(key)))
		{
			const auto at = button->GetAbsoluteOffset(Rml::BoxArea::Border);
			navigation_buttons_.push_back({at.x, at.y, button->GetOffsetWidth(), button->GetOffsetHeight()});
		}
	}
	if (navigation_visible_)
	{
		frame.output.controls.emplace_back(
		    frame.id("nav.map"), localise("Navigation map. Arrow keys select a position. Page Up and Page Down "
		                                  "zoom. Tab reaches the waypoint action."));
		frame.output.place(dom_, frame.context, region, frame.id("nav.map"));
	}
}
bool NavigationPanel::action(ConsoleModel &model_, ConsoleDocument &dom, const std::string &prefix,
                             const std::function<void()> &return_home, const std::string &action)
{
	auto *document_ = &dom.document();
	const auto panel_id = [&](const std::string &name)
	{
		return prefix + name;
	};

	if (action == "mission.leave")
	{
		if (!mission_outcome_.empty() && return_home)
			return_home();
		return true;
	}
	if (!navigation_visible_ || !action.starts_with("nav."))
		return false;
	if (action == "nav.map")
		document_->GetElementById(panel_id("nav.map"))->Focus();
	else if (action == "nav.reset")
		navigation_camera_.automatic = true;
	else if (action == "nav.in")
		navigation_camera_.zoom(.8);
	else if (action == "nav.out")
		navigation_camera_.zoom(1.25);
	else if (action == "nav.left")
		navigation_camera_.pan(80, 0);
	else if (action == "nav.right")
		navigation_camera_.pan(-80, 0);
	else if (action == "nav.up")
		navigation_camera_.pan(0, 80);
	else if (action == "nav.down")
		navigation_camera_.pan(0, -80);
	else if (action == "nav.clear")
		model_.set_waypoint(0, 0, false);
	else if (action == "nav.set" && navigation_selected_)
		model_.set_waypoint(static_cast<std::int64_t>(navigation_x_), static_cast<std::int64_t>(navigation_y_));
	else if (action.starts_with("nav.objective/"))
		for (const auto &objective : model_.objectives())
			if (objective.contains("destination_x") &&
			    std::to_string(objective.at("id").get<std::uint64_t>()) == action.substr(14))
				model_.set_waypoint(objective.at("destination_x"), objective.at("destination_y"));
	return true;
}
NavigationPointerResult NavigationPanel::pointer(ConsoleModel &model_, std::uint64_t contact, float x, float y,
                                                 DeviceEventKind kind)
{
	bool changed = false;

	if (!navigation_visible_)
		return {false, changed};
	if (kind == DeviceEventKind::Down)
		for (const auto &button : navigation_buttons_)
			if (CheckCollisionPointRec({x, y}, button))
				return {false, changed};
	if (kind == DeviceEventKind::Down && CheckCollisionPointRec({x, y}, navigation_area_))
	{
		if (navigation_contacts_.empty())
			navigation_dragged_ = false;
		navigation_contacts_[contact] = navigation_starts_[contact] = {x, y};
		return {true, changed};
	}
	if (!navigation_contacts_.contains(contact))
		return {false, changed};
	const auto old = navigation_contacts_.at(contact);
	if (kind == DeviceEventKind::Cancel)
		navigation_dragged_ = true;
	else if (navigation_contacts_.size() == 2)
	{
		for (const auto &[other, point] : navigation_contacts_)
			if (other != contact)
			{
				const auto before = std::hypot(old.x - point.x, old.y - point.y),
				           after = std::hypot(x - point.x, y - point.y);
				if (before > 5 && after > 5)
					navigation_camera_.zoom(before / after,
					                        (x + point.x) / 2 - navigation_area_.x - navigation_area_.width / 2,
					                        (y + point.y) / 2 - navigation_area_.y - navigation_area_.height / 2);
			}
		navigation_dragged_ = true;
	}
	else
	{
		const auto start = navigation_starts_.at(contact);
		if (std::hypot(x - start.x, y - start.y) > 5)
			navigation_dragged_ = true;
		if (navigation_dragged_)
			navigation_camera_.pan(x - old.x, y - old.y);
		else if (kind == DeviceEventKind::Up)
		{
			const double wx =
			    navigation_camera_.x + (x - navigation_area_.x - navigation_area_.width / 2) * navigation_camera_.scale;
			const double wy = navigation_camera_.y -
			                  (y - navigation_area_.y - navigation_area_.height / 2) * navigation_camera_.scale;
			navigation_x_ = std::clamp(wx, -1e9, 1e9);
			navigation_y_ = std::clamp(wy, -1e9, 1e9);
			navigation_selected_ = true;
			navigation_selected_contact_ = 0;
			if (model_.observation())
				for (const auto &body : model_.observation()->contacts)
					if (std::hypot(wx - body.x, wy - body.y) <= navigation_camera_.scale * 18)
					{
						navigation_selected_contact_ = body.id;
						navigation_x_ = body.x;
						navigation_y_ = body.y;
						break;
					}
			changed = true;
		}
	}
	if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
	{
		navigation_contacts_.erase(contact);
		navigation_starts_.erase(contact);
	}
	else
		navigation_contacts_[contact] = {x, y};
	return {true, changed};
}
bool NavigationPanel::key(int key, ConsoleDocument &dom, const std::string &prefix)
{
	auto *document_ = &dom.document();
	const auto panel_id = [&](const std::string &name)
	{
		return prefix + name;
	};
	if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_UP ||
	    key == Rml::Input::KI_DOWN)
	{
		if (!navigation_selected_)
		{
			navigation_x_ = navigation_camera_.x;
			navigation_y_ = navigation_camera_.y;
		}
		navigation_selected_ = true;
		navigation_selected_contact_ = 0;
		navigation_x_ += (key == Rml::Input::KI_LEFT    ? -40
		                  : key == Rml::Input::KI_RIGHT ? 40
		                                                : 0) *
		                 navigation_camera_.scale;
		navigation_y_ += (key == Rml::Input::KI_DOWN ? -40
		                  : key == Rml::Input::KI_UP ? 40
		                                             : 0) *
		                 navigation_camera_.scale;
	}
	else if (key == Rml::Input::KI_PRIOR || key == Rml::Input::KI_NEXT)
		navigation_camera_.zoom(key == Rml::Input::KI_PRIOR ? .8 : 1.25);
	else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_SPACE)
	{
		navigation_selected_ = true;
		if (auto *button = document_->GetElementById(panel_id("nav.set")))
			button->Focus();
	}
	else
		return false;
	return true;
}
void NavigationPanel::wheel(float x, float y, float delta)
{
	if (CheckCollisionPointRec({x, y}, navigation_area_))
		navigation_camera_.zoom(std::pow(1.2, delta), x - navigation_area_.width / 2,
		                        y - navigation_area_.y - navigation_area_.height / 2);
}
void NavigationPanel::draw(const ConsoleModel &model_, TextureCache &radar_textures_, const Catalog &catalog,
                           const Json &tokens, bool settings_open, float ox, float oy)
{

	if (!navigation_visible_ || !mission_outcome_.empty() || settings_open)
		return;
	if (!model_.observation())
		return;
	const auto &view = *model_.observation();
	draw_navigation_plot(
	    view.own, view.regions, Json::array(), model_.waypoint(), navigation_camera_,
	    {ox + navigation_area_.x, oy + navigation_area_.y, navigation_area_.width, navigation_area_.height}, false,
	    radar_selection_colour(catalog, "waypoint", tokens));
	const Rectangle area{ox + navigation_area_.x, oy + navigation_area_.y, navigation_area_.width,
	                     navigation_area_.height};
	BeginScissorMode(static_cast<int>(area.x), static_cast<int>(area.y), static_cast<int>(area.width),
	                 static_cast<int>(area.height));
	for (const auto &contact : view.contacts)
	{
		const Vector2 at{area.x + area.width / 2 +
		                     static_cast<float>((contact.x - navigation_camera_.x) / navigation_camera_.scale),
		                 area.y + area.height / 2 -
		                     static_cast<float>((contact.y - navigation_camera_.y) / navigation_camera_.scale)};
		draw_radar_body(radar_textures_, catalog, contact, at, 9, SKYBLUE);
	}
	if (navigation_selected_)
	{
		const Vector2 at{area.x + area.width / 2 +
		                     static_cast<float>((navigation_x_ - navigation_camera_.x) / navigation_camera_.scale),
		                 area.y + area.height / 2 -
		                     static_cast<float>((navigation_y_ - navigation_camera_.y) / navigation_camera_.scale)};
		draw_radar_selection(at, 14, radar_selection_colour(catalog, "waypoint", tokens));
	}
	EndScissorMode();
}
NavigationBrowsing NavigationPanel::browsing() const
{
	return {navigation_camera_, navigation_x_, navigation_y_};
}
void NavigationPanel::restore_browsing(const NavigationBrowsing &saved)
{
	navigation_camera_ = saved.camera;
	navigation_x_ = saved.x;
	navigation_y_ = saved.y;
}
std::vector<std::int64_t> NavigationPanel::display_camera() const
{
	return {static_cast<std::int64_t>(std::clamp(navigation_camera_.x, -1e9, 1e9)),
	        static_cast<std::int64_t>(std::clamp(navigation_camera_.y, -1e9, 1e9)),
	        static_cast<std::int64_t>(std::clamp(navigation_camera_.scale * navigation_area_.width / 100, 1.0, 1e9))};
}
void NavigationPanel::layout_area(Rectangle area)
{
	navigation_area_ = area;
}
void NavigationPanel::hide()
{
	navigation_visible_ = false;
}
void NavigationPanel::cancel()
{
	navigation_contacts_.clear();
	navigation_starts_.clear();
}
void NavigationPanel::coordinate(bool x, double value)
{
	(x ? navigation_x_ : navigation_y_) = value;
}
bool NavigationPanel::visible() const
{
	return navigation_visible_;
}
bool NavigationPanel::selected() const
{
	return navigation_selected_;
}
double NavigationPanel::selected_x() const
{
	return navigation_x_;
}
Rectangle NavigationPanel::area() const
{
	return navigation_area_;
}
bool NavigationPanel::captures(std::uint64_t contact) const
{
	return navigation_contacts_.contains(contact);
}
const std::string &NavigationPanel::outcome() const
{
	return mission_outcome_;
}
} // namespace phoenix
