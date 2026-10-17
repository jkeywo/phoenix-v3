#pragma once
#include <phoenix/session.hpp>
#include <phoenix/world.hpp>
namespace phoenix {
// Server-only composition boundary. Frontends never receive this interface.
class HostAccess : public SessionPort {
public:
    virtual World& authoritative_world() = 0;
    virtual const World& authoritative_world() const = 0;
    virtual const std::vector<Bytes>& content_archives() const = 0;
    virtual const GameSetup& setup() const = 0;
};
std::shared_ptr<HostAccess> make_host_world();
}
