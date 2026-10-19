module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>
#include <utility>

export module phoenix.session.api;
export import phoenix.content;
export import phoenix.contracts;
export import phoenix.transport;

export namespace phoenix
{
enum class JoinRole
{
	ship_server,
	player,
	gm
};
struct LobbySlot
{
	std::string id, ship, host;
	bool mandatory = false;
	std::vector<std::string> allowed_ships;
};
struct LobbyMember
{
	std::string id, slot, console;
	JoinRole role = JoinRole::player;
	bool approved = false, connected = true, ready = false, verified = false;
	std::string name;
	Bytes avatar;
	std::uint64_t local_screen = 0;
};
struct LobbyConsole
{
	std::string slot, id, name, member;
	std::uint64_t local_screen = 0;
	bool ready = false;
	std::string profile;
	std::vector<std::string> profiles;
};
struct LobbyProjection
{
	bool active = false, coordinator = false, launched = false;
	std::string self, scenario, code, status;
	int countdown_ms = -1;
	std::vector<LobbySlot> slots;
	std::vector<LobbyMember> members;
	std::vector<LobbyConsole> consoles;
};
enum class LobbyAction
{
	claim_ship,
	select_console,
	approve,
	ready,
	unready,
	leave,
	assign_local_console,
	select_profile
};
struct LobbyCommand
{
	LobbyAction action;
	std::string member, slot, ship, console;
	std::uint64_t screen = 0;
	std::string profile;
};
struct GameSetup
{
	std::string mission;
	std::map<std::string, std::string> ships;
	std::string local_slot;
	std::string local_console;
	std::map<std::string, std::map<std::string, std::string>> console_profiles;
};
struct ConsoleSeat
{
	ConsoleAddress address;
	ConsoleSpec spec;
	bool available = false;
};
struct LobbyAdmission
{
	std::string key;
	JoinRole role = JoinRole::player;
};
struct NetworkOptions
{
	LinkOptions link;
	std::string server, data_location = "userdata", profile = "default";
	bool inspector = false;
	std::string admission;
	std::map<std::string, LobbyAdmission> lobby_tickets;
	std::string player_name = "Player";
	Bytes avatar;
	// Local journal compaction policy, independent of gameplay and peer identity.
	std::uint64_t checkpoint_frames = 4096;
};
// The frontend sees content and projections, never the authoritative ECS.
class SessionPort
{
public:
	virtual ~SessionPort() = default;
	// Changes only when the session's world is replaced or cleared, never for a tick or claim.
	virtual std::uint64_t generation() const
	{
		return 0;
	}
	virtual void add_pack(const Bytes &archive) = 0;
	virtual void clear_packs() = 0;
	virtual const Catalog &catalog() const = 0;
	virtual void start(const GameSetup &setup) = 0;
	virtual void prepare_lobby(const GameSetup &setup)
	{
		start(setup);
	}
	virtual LobbyProjection lobby() const
	{
		return {};
	}
	virtual bool set_player_profile(const std::string &, const Bytes &)
	{
		return false;
	}
	virtual bool set_local_player_profile(std::uint64_t, const std::string &, const Bytes &)
	{
		return false;
	}
	virtual bool lobby_command(const LobbyCommand &)
	{
		return false;
	}
	virtual void replace_packs(const std::vector<Bytes> &)
	{
		throw std::runtime_error("Pack selection is unavailable");
	}
	virtual bool running() const = 0;
	virtual bool recovery_pending() const = 0;
	virtual Bytes save() const = 0;
	virtual Bytes fleet_setup(const GameSetup &) const
	{
		return {};
	}
	virtual void restore(const Bytes &archive) = 0;
	virtual void resume_local() = 0;
	virtual void activate_prepared()
	{
		resume_local();
	}
	virtual void authorise_lobby_member(const std::string &, const LobbyAdmission &)
	{
	}
	virtual void tick() = 0;
	virtual std::uint64_t ticks() const = 0;
	virtual bool command(const OperatingCommand &command) = 0;
	virtual std::optional<Observation> observation() const = 0;
	virtual std::vector<BodyView> viewscreen() const = 0;
	virtual std::vector<SceneBody> scene() const
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
	virtual Json scene_lighting() const
	{
		return Json::object();
	}
	virtual Json scene_presentation() const
	{
		return Json::object();
	}
	// Read-only public board; never a Controller inbox or command projection.
	virtual Json public_board() const
	{
		Json result{{"shared", Json::array()}, {"messages", Json::array()}};
		if (const auto view = observation(); view && view->gameplay.is_object())
		{
			result["shared"] = view->gameplay.value("shared", Json::array());
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
	virtual std::vector<ConsoleSeat> console_seats() const
	{
		return {};
	}
	virtual bool claim_console(std::uint64_t, const ConsoleAddress &)
	{
		return false;
	}
	virtual bool release_console(std::uint64_t)
	{
		return false;
	}
	virtual bool console_command(std::uint64_t route, const OperatingCommand &value)
	{
		return route == 1 && command(value);
	}
	virtual std::optional<Observation> console_observation(std::uint64_t route) const
	{
		return route == 1 ? observation() : std::nullopt;
	}
	virtual void poll()
	{
	}
	virtual bool networked() const
	{
		return false;
	}
	virtual std::string connection_status() const
	{
		return {};
	}
	virtual std::string invitation_code() const
	{
		return {};
	}
	virtual std::vector<std::pair<std::string, std::string>> lobby_controls() const
	{
		return {};
	}
	virtual bool session_action(const std::string &)
	{
		return false;
	}
	virtual Json inspection() const
	{
		return nullptr;
	}
	virtual std::optional<BodyView> focus_body() const
	{
		auto value = observation();
		return value ? std::optional(value->own) : std::nullopt;
	}
};
} // namespace phoenix
