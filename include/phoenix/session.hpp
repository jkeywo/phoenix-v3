#pragma once
#include <phoenix/content.hpp>
#include <phoenix/contracts.hpp>
#include <memory>
#include <optional>
namespace phoenix {
struct GameSetup {
    std::string mission;
    std::map<std::string,std::string> ships;
    std::string local_slot;
};
// The frontend sees content and projections, never the authoritative ECS.
// This first adapter is a single, local ship server; it is not fleet consensus.
class SessionPort {
public:
    virtual ~SessionPort() = default;
    virtual void add_pack(const Bytes& archive) = 0;
    virtual void clear_packs() = 0;
    virtual const Catalog& catalog() const = 0;
    virtual void start(const GameSetup& setup) = 0;
    virtual bool running() const = 0;
    virtual void tick() = 0;
    virtual std::uint64_t ticks() const = 0;
    virtual bool command(const OperatingCommand& command) = 0;
    virtual std::optional<Observation> observation() const = 0;
    virtual std::vector<BodyView> viewscreen() const = 0;
};
std::shared_ptr<SessionPort> make_local_session();
}
