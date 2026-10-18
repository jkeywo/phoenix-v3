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
};
struct LobbyProjection
{
	bool active = false, coordinator = false, launched = false;
	std::string self, scenario, code, status;
	int countdown_ms = -1;
	std::vector<LobbySlot> slots;
	std::vector<LobbyMember> members;
};
enum class LobbyAction
{
	claim_ship,
	select_console,
	approve,
	ready,
	unready,
	leave
};
struct LobbyCommand
{
	LobbyAction action;
	std::string member, slot, ship, console;
};
struct GameSetup
{
	std::string mission;
	std::map<std::string, std::string> ships;
	std::string local_slot;
	std::string local_console;
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
