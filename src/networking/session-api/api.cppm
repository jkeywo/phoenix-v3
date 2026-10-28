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
export import phoenix.content.catalog;
export import phoenix.content.controllers.types;
export import phoenix.contracts;
export import phoenix.transport;
export import phoenix.inference;

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
	std::string briefing;
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
	std::string description;
	std::map<std::string, int> profile_stars;
};
struct LobbyProjection
{
	bool active = false, coordinator = false, launched = false;
	std::string self, scenario, code, status;
	std::string scenario_name;
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
	bool gm_host = false;
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
	BaseConfiguration expected_base;
	std::map<std::string, std::string> pack_urls;
	std::map<std::string, Bytes> prepared_clients;
	// Session-local outcome carried from lobby preparation; retry changed descriptors.
	Json unavailable_client;
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
	virtual ~SessionPort();
	// Changes only when the session's world is replaced or cleared, never for a tick or claim.
	virtual std::uint64_t generation() const;
	virtual void add_pack(const Bytes &archive) = 0;
	virtual void clear_packs() = 0;
	virtual const Catalog &catalog() const = 0;
	virtual std::string loading_scenario() const;
	virtual std::optional<std::pair<std::uint64_t, std::uint64_t>> content_progress() const;
	virtual std::string preparation_phase() const;
	virtual void start(const GameSetup &setup) = 0;
	virtual void prepare_lobby(const GameSetup &setup);
	virtual LobbyProjection lobby() const;
	virtual bool set_player_profile(const std::string &, const Bytes &);
	virtual bool set_local_player_profile(std::uint64_t, const std::string &, const Bytes &);
	virtual bool lobby_command(const LobbyCommand &);
	virtual void replace_packs(const std::vector<Bytes> &);
	virtual bool running() const = 0;
	virtual bool recovery_pending() const = 0;
	virtual Bytes save() const = 0;
	virtual Bytes fleet_setup(const GameSetup &) const;
	virtual void restore(const Bytes &archive) = 0;
	virtual void resume_local() = 0;
	virtual void activate_prepared();
	virtual void authorise_lobby_member(const std::string &, const LobbyAdmission &);
	virtual void tick() = 0;
	virtual std::uint64_t ticks() const = 0;
	virtual bool command(const OperatingCommand &command) = 0;
	virtual std::optional<Observation> observation() const = 0;
	virtual std::vector<BodyView> viewscreen() const = 0;
	virtual std::shared_ptr<const std::vector<SceneBody>> scene_snapshot() const;
	virtual std::vector<SceneBody> scene() const;
	virtual Json scene_lighting() const;
	virtual Json scene_presentation() const;
	// Read-only public board; never a Controller inbox or command projection.
	virtual Json public_board() const;
	virtual std::vector<ConsoleSeat> console_seats() const;
	virtual bool claim_console(std::uint64_t, const ConsoleAddress &);
	virtual bool release_console(std::uint64_t);
	virtual bool console_command(std::uint64_t route, const OperatingCommand &value);
	// UI delivery receipt: the authoritative session still admits the command.
	// Actor/network adapters may queue delivery; direct sessions retain synchronous admission.
	virtual bool dispatch_console_command(std::uint64_t route, const OperatingCommand &value);
	virtual std::optional<Observation> console_observation(std::uint64_t route) const;
	virtual std::shared_ptr<const Observation> console_snapshot(std::uint64_t route) const;
	virtual void set_inference_connection(std::shared_ptr<InferenceConnection>);
	virtual Json inference_state() const;
	virtual bool inference_command(const Json &);
	virtual void poll();
	virtual bool networked() const;
	virtual std::string connection_status() const;
	virtual std::string invitation_code() const;
	virtual std::vector<std::pair<std::string, std::string>> lobby_controls() const;
	virtual bool session_action(const std::string &);
	virtual Json inspection() const;
	virtual std::optional<BodyView> focus_body() const;
};
} // namespace phoenix
