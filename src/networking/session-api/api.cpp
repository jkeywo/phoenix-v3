module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.session.api;

namespace phoenix
{
SessionPort::~SessionPort() = default;
std::uint64_t SessionPort::generation() const
{
	return 0;
}

std::string SessionPort::loading_scenario() const
{
	return {};
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> SessionPort::content_progress() const
{
	return {};
}

std::string SessionPort::preparation_phase() const
{
	return {};
}

void SessionPort::prepare_lobby(const GameSetup &setup)
{
	start(setup);
}

LobbyProjection SessionPort::lobby() const
{
	return {};
}

bool SessionPort::set_player_profile(const std::string &, const Bytes &)
{
	return false;
}

bool SessionPort::set_local_player_profile(std::uint64_t, const std::string &, const Bytes &)
{
	return false;
}

bool SessionPort::lobby_command(const LobbyCommand &)
{
	return false;
}

void SessionPort::replace_packs(const std::vector<Bytes> &)
{
	throw std::runtime_error("Pack selection is unavailable");
}

Bytes SessionPort::fleet_setup(const GameSetup &) const
{
	return {};
}

void SessionPort::activate_prepared()
{
	resume_local();
}

void SessionPort::authorise_lobby_member(const std::string &, const LobbyAdmission &)
{
}

std::shared_ptr<const std::vector<SceneBody>> SessionPort::scene_snapshot() const
{
	return {};
}

std::vector<SceneBody> SessionPort::scene() const
{
	std::vector<SceneBody> result;
	for (const auto &body : viewscreen())
	{
		SceneBody item;
		static_cast<BodyView &>(item) = body;
		result.push_back(std::move(item));
	}
	return result;
}

Json SessionPort::scene_lighting() const
{
	return Json::object();
}

Json SessionPort::scene_presentation() const
{
	return Json::object();
}

Json SessionPort::public_board() const
{
	Json result{{"shared", Json::array()}, {"messages", Json::array()}};
	if (const auto view = observation(); view && view->gameplay.is_object())
	{
		result["shared"] = view->gameplay.value("shared", Json::array());
		result["alert"] = view->gameplay.value("alert", Json::object());
		for (auto message : view->gameplay.value("messages", Json::array()))
			if (message.is_object() && message.value("public", false))
			{
				message["actionable"] = false;
				message["offer_only"] = true;
				result["messages"].push_back(std::move(message));
			}
		result["omitted"] = view->gameplay.value("omitted", Json::object());
	}
	return result;
}

std::vector<ConsoleSeat> SessionPort::console_seats() const
{
	return {};
}

bool SessionPort::claim_console(std::uint64_t, const ConsoleAddress &)
{
	return false;
}

bool SessionPort::release_console(std::uint64_t)
{
	return false;
}

bool SessionPort::console_command(std::uint64_t route, const OperatingCommand &value)
{
	return route == 1 && command(value);
}

bool SessionPort::dispatch_console_command(std::uint64_t route, const OperatingCommand &value)
{
	return console_command(route, value);
}

std::optional<Observation> SessionPort::console_observation(std::uint64_t route) const
{
	return route == 1 ? observation() : std::nullopt;
}

std::shared_ptr<const Observation> SessionPort::console_snapshot(std::uint64_t route) const
{
	auto value = console_observation(route);
	return value ? std::make_shared<const Observation>(std::move(*value)) : nullptr;
}

void SessionPort::set_inference_connection(std::shared_ptr<InferenceConnection>)
{
}

Json SessionPort::inference_state() const
{
	return Json::object();
}

bool SessionPort::inference_command(const Json &)
{
	return false;
}

void SessionPort::poll()
{
}

bool SessionPort::networked() const
{
	return false;
}

std::string SessionPort::connection_status() const
{
	return {};
}

std::string SessionPort::invitation_code() const
{
	return {};
}

std::vector<std::pair<std::string, std::string>> SessionPort::lobby_controls() const
{
	return {};
}

bool SessionPort::session_action(const std::string &)
{
	return false;
}

Json SessionPort::inspection() const
{
	return nullptr;
}

std::optional<BodyView> SessionPort::focus_body() const
{
	auto value = observation();
	return value ? std::optional(value->own) : std::nullopt;
}
} // namespace phoenix
