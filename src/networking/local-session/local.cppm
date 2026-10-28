module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.session.local;
export import phoenix.session.api;
export import phoenix.simulation;

export namespace phoenix
{
// Server-only composition boundary. Frontends never receive this interface.
class HostAccess : public SessionPort
{
public:
	virtual World &authoritative_world() = 0;
	virtual const World &authoritative_world() const = 0;
	virtual const std::vector<Pack> &content_packs() const = 0;
	virtual void add_verified_pack(const Pack &pack) = 0;
	virtual void start_server(const GameSetup &setup) = 0;
	virtual void resume_server() = 0;
	virtual PackDraft save_container() const = 0;
	virtual void restore_container(const PackDraft &container) = 0;
	virtual const GameSetup &setup() const = 0;
};
std::shared_ptr<HostAccess> make_host_world();
} // namespace phoenix

export namespace phoenix
{
std::shared_ptr<SessionPort> make_local_session();
// Explicit frontend composition: fixed ticks and projection production run away
// from the render loop; the authoritative session still owns all admission.
std::shared_ptr<SessionPort> make_frame_session(std::shared_ptr<SessionPort> session);
Json frame_session_profile(const std::shared_ptr<SessionPort> &session);
Bytes pause_frame_session_checkpoint(const std::shared_ptr<SessionPort> &session);
} // namespace phoenix
