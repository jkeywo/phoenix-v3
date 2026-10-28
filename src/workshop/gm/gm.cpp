module;
#include <imgui.h>
#include <imgui_internal.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

module phoenix.workshop.gm;
import phoenix.content.assets;
import phoenix.graphics.assets;
import phoenix.workshop.map_navigation;

namespace phoenix
{
namespace
{
std::string gm_window_label(const char *text)
{
	return localise(text) + "###GM." + text;
}
} // namespace
struct GmInspector::State
{
	std::weak_ptr<SessionPort> session;
	std::uint64_t selected = 0;
	float pan_x = 0, pan_y = 0, zoom = .004f;
	ImVec2 dock_size{};
	TextureCache textures;
	MapGesture gesture;
};
GmInspector::GmInspector() : state_(std::make_unique<State>())
{
}
GmInspector::~GmInspector() = default;
void GmInspector::clear()
{
	state_ = std::make_unique<State>();
}
void GmInspector::draw(const std::shared_ptr<SessionPort> &owner, const std::string &status, const ToolFrame &frame)
{
	if (state_->session.lock() != owner)
	{
		clear();
		state_->session = owner;
	}
	const SessionPort *session = owner.get();
	if (session)
		apply_tool_theme(session->catalog());
	const auto dock = ImGui::GetID("PhoenixGmDock");
	auto *viewport = ImGui::GetMainViewport();
	if (state_->dock_size.x != viewport->WorkSize.x || state_->dock_size.y != viewport->WorkSize.y)
	{
		state_->dock_size = viewport->WorkSize;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dock, viewport->WorkSize);
		ImGuiID centre = dock, left, right, bottom;
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, .10f, &bottom, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, .20f, &left, &centre);
		ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, .38f, &right, &centre);
		ImGui::DockBuilderDockWindow(gm_window_label("Status").c_str(), bottom);
		ImGui::DockBuilderDockWindow(gm_window_label("Entities").c_str(), left);
		ImGui::DockBuilderDockWindow(gm_window_label("Properties").c_str(), right);
		ImGui::DockBuilderDockWindow(gm_window_label("Map").c_str(), centre);
		ImGui::DockBuilderFinish(dock);
	}
	ImGui::DockSpaceOverViewport(dock, viewport);
	const auto state = session ? session->inspection() : Json(nullptr);
	tool_window(gm_window_label("Status").c_str());
	if (!status.empty())
		ImGui::TextWrapped("%s", localise(status).c_str());
	ImGui::TextWrapped("%s", session ? session->connection_status().c_str()
	                                 : localise("Connect to a ship server to request read-only inspection.").c_str());
	ImGui::End();
	if (state.is_null())
	{
		tool_window(gm_window_label("Map").c_str());
		ImGui::TextWrapped("%s", session && session->lobby().active
		                             ? localise("GM screen claimed. Inspection begins when the game launches.").c_str()
		                             : localise("Waiting for host approval. Live world data is unavailable.").c_str());
		ImGui::End();
		return;
	}
	tool_window(gm_window_label("Entities").c_str());
	for (const auto &value : state.at("bodies"))
	{
		const auto &body = value.at("body");
		const auto id = body.at("id").get<std::uint64_t>();
		const auto label = std::to_string(id) + " / " + body.at("definition").get<std::string>();
		if (ImGui::Selectable(label.c_str(), state_->selected == id))
			state_->selected = id;
	}
	ImGui::End();
	ImGui::SetNextWindowContentSize({ImGui::GetFontSize() * 32.f, 0});
	tool_window(gm_window_label("Properties").c_str());
	for (const auto &value : state.at("bodies"))
		if (value.at("body").at("id") == state_->selected)
		{
			const auto details = value.dump(2);
			ImGui::TextUnformatted(details.c_str());
		}
	ImGui::End();
	tool_window(gm_window_label("Map").c_str());
	ImGui::TextUnformatted(
	    ToolMessage("Committed tick {tick} / read only", {{"tick", std::to_string(session->ticks())}}).c_str());
	const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("Live map", ImVec2(std::max(1.f, size.x), std::max(1.f, size.y)),
	                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	if (!IsWindowFocused())
		state_->gesture.contacts.clear();
	for (auto event : frame.pointers)
	{
		event.x -= frame.area.x;
		event.y -= frame.area.y;
		const bool inside = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		                    event.x >= origin.x && event.y >= origin.y && event.x < origin.x + size.x &&
		                    event.y < origin.y + size.y;
		state_->gesture.event(event, inside, origin.x + size.x / 2, origin.y + size.y / 2, 0.000001f, 1.f,
		                      state_->pan_x, state_->pan_y, state_->zoom);
	}
	if (ImGui::IsItemHovered())
	{
		auto &io = ImGui::GetIO();
		state_->zoom = std::clamp(state_->zoom * std::pow(1.15f, io.MouseWheel), 0.000001f, 1.f);
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
		{
			state_->pan_x += io.MouseDelta.x;
			state_->pan_y += io.MouseDelta.y;
		}
	}
	auto *draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	for (const auto &value : state.at("bodies"))
	{
		const auto &body = value.at("body");
		const auto id = body.at("id").get<std::uint64_t>();
		ImVec2 point(origin.x + size.x / 2 + state_->pan_x + body.at("position").at("x").get<float>() * state_->zoom,
		             origin.y + size.y / 2 + state_->pan_y - body.at("position").at("y").get<float>() * state_->zoom);
		RadarIcon icon = RadarIcon::contact;
		Texture2D sprite{};
		const auto &entities = session->catalog().entities;
		const auto definition = entities.find(body.at("definition").get<std::string>());
		if (definition != entities.end())
		{
			icon = render_radar_icon(definition->second.at("render"));
			const auto reference = radar_sprite_reference(definition->second.at("render"));
			if (!reference.empty())
				sprite = state_->textures.load(reference, session->catalog().assets.at(reference));
		}
		draw_tool_radar_icon(draw, icon, point, body.at("heading").get<float>(),
		                     state_->selected == id ? IM_COL32(255, 220, 100, 255) : IM_COL32(180, 225, 240, 255),
		                     sprite);
		auto label = std::to_string(id);
		draw->AddText(ImVec2(point.x + 14, point.y), IM_COL32_WHITE, label.c_str());
		const auto mouse = ImGui::GetIO().MousePos;
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		    std::hypot(mouse.x - point.x, mouse.y - point.y) < 12)
			state_->selected = id;
	}
	draw->PopClipRect();
	ImGui::End();
	return;
}
} // namespace phoenix
