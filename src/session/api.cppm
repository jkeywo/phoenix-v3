module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.session.api;
export import phoenix.content;
export import phoenix.contracts;
export import phoenix.transport;

export namespace phoenix
{
struct GameSetup
{
	std::string mission;
	std::map<std::string, std::string> ships;
	std::string local_slot;
};
struct NetworkOptions
{
	LinkOptions link;
	std::string server, data_location = "userdata", profile = "default";
	bool inspector = false;
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
	virtual bool running() const = 0;
	virtual bool recovery_pending() const = 0;
	virtual Bytes save() const = 0;
	virtual void restore(const Bytes &archive) = 0;
	virtual void resume_local() = 0;
	virtual void tick() = 0;
	virtual std::uint64_t ticks() const = 0;
	virtual bool command(const OperatingCommand &command) = 0;
	virtual std::optional<Observation> observation() const = 0;
	virtual std::vector<BodyView> viewscreen() const = 0;
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
